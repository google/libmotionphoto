// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A ContainerSplitter for HEIF-based images (HEIC and AVIF). It reads the ISO
// BMFF item boxes directly instead of going through libheif, so motion photo
// detection also works in builds without libheif, such as Android.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "image_io/base/data_segment.h"
#include "image_io/base/data_source.h"
#include "motion_photo/container_splitter.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/mpvd_box.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

// Limits that keep malformed or hostile input from driving large allocations
// or long loops.
constexpr size_t kMaxTopLevelBoxes = 1024;
constexpr size_t kMaxChildBoxes = 4096;
constexpr uint64_t kMaxMetaPayloadSize = 16 * 1024 * 1024;
constexpr uint64_t kMaxXmpItemSize = 16 * 1024 * 1024;
constexpr size_t kMaxXmpItems = 16;
constexpr uint64_t kMaxItemExtents = 256;

// ISO/IEC 23008-12 stores XMP as a 'mime' item with this content type.
constexpr std::string_view kXmpContentType = "application/rdf+xml";

// Copies `length` bytes at file offset `start` into `out`. Returns false if the
// data source cannot supply the whole range.
bool ReadFromDataSource(image_io::DataSource* data_source, size_t start,
                        size_t length, uint8_t* out) {
  size_t copied = 0;
  while (copied < length) {
    const size_t chunk_start = start + copied;
    std::shared_ptr<image_io::DataSegment> segment =
        data_source->GetDataSegment(chunk_start, length - copied);
    if (segment == nullptr) {
      return false;
    }
    // A non-null buffer means `chunk_start` lies inside the segment, so each
    // iteration copies at least one byte.
    const uint8_t* buffer = segment->GetBuffer(chunk_start);
    if (buffer == nullptr) {
      return false;
    }
    const size_t chunk_size =
        std::min(length - copied, segment->GetEnd() - chunk_start);
    std::copy(buffer, buffer + chunk_size, out + copied);
    copied += chunk_size;
  }
  return true;
}

// Appends `length` bytes at file offset `start` to `out`.
bool AppendFromDataSource(image_io::DataSource* data_source, size_t start,
                          size_t length, std::vector<uint8_t>* out) {
  const size_t old_size = out->size();
  out->resize(old_size + length);
  return ReadFromDataSource(data_source, start, length, out->data() + old_size);
}

// A bounds-checked big-endian reader over an in-memory byte range. Reading past
// the end puts the reader into a sticky failed state in which every read
// returns zero or an empty value.
class BoxReader {
 public:
  BoxReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool ok() const { return ok_; }

  // Returns the number of unread bytes, or 0 after a failed read.
  size_t remaining() const { return ok_ ? size_ - position_ : 0; }

  // Returns a pointer to the first unread byte.
  const uint8_t* current() const { return data_ + position_; }

  // Reads an unsigned big-endian integer that is `num_bytes` (0 to 8) long.
  uint64_t ReadUInt(size_t num_bytes) {
    if (!ok_ || num_bytes > sizeof(uint64_t) || num_bytes > size_ - position_) {
      ok_ = false;
      return 0;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < num_bytes; ++i) {
      value = (value << 8) | data_[position_ + i];
    }
    position_ += num_bytes;
    return value;
  }

  // Reads the version of a FullBox header and skips its 24-bit flags.
  uint64_t ReadFullBoxVersion() {
    const uint64_t version = ReadUInt(1);
    Skip(3);
    return version;
  }

  // Reads a four-character code such as a box type or an item type.
  std::string_view ReadFourCc() {
    if (!ok_ || size_ - position_ < 4) {
      ok_ = false;
      return {};
    }
    std::string_view four_cc(reinterpret_cast<const char*>(data_ + position_),
                             4);
    position_ += 4;
    return four_cc;
  }

  // Reads a NUL-terminated string. A string that runs to the end of the range
  // is accepted without a terminator, because some writers omit the last NUL.
  std::string_view ReadCString() {
    const size_t available = remaining();
    if (available == 0) {
      return {};
    }
    const char* begin = reinterpret_cast<const char*>(data_ + position_);
    const void* terminator = std::memchr(begin, '\0', available);
    if (terminator == nullptr) {
      position_ = size_;
      return std::string_view(begin, available);
    }
    const size_t length = static_cast<const char*>(terminator) - begin;
    position_ += length + 1;
    return std::string_view(begin, length);
  }

  void Skip(size_t num_bytes) {
    if (!ok_ || num_bytes > size_ - position_) {
      ok_ = false;
      return;
    }
    position_ += num_bytes;
  }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t position_ = 0;
  bool ok_ = true;
};

struct BoxHeader {
  std::string_view type;
  size_t header_size = 0;
  // The size of the whole box, including the header.
  size_t box_size = 0;
};

// Reads the box header at the reader's position. `available` is the number of
// bytes from the start of the box to the end of its parent. Returns nullopt if
// the header is malformed or the box overruns its parent.
std::optional<BoxHeader> ReadBoxHeader(BoxReader* reader, size_t available) {
  BoxHeader header;
  uint64_t box_size = reader->ReadUInt(4);
  header.type = reader->ReadFourCc();
  header.header_size = 8;
  if (box_size == 1) {
    box_size = reader->ReadUInt(8);
    header.header_size = 16;
  } else if (box_size == 0) {
    // The box extends to the end of its parent.
    box_size = available;
  }
  if (!reader->ok() || box_size < header.header_size || box_size > available) {
    return std::nullopt;
  }
  header.box_size = static_cast<size_t>(box_size);
  return header;
}

// Calls `visitor(type, payload, payload_size)` for each box in the in-memory
// range [data, data + size), in order. Stops when the visitor returns false or
// at the first malformed box.
template <typename Visitor>
void ForEachBox(const uint8_t* data, size_t size, Visitor visitor) {
  BoxReader reader(data, size);
  for (size_t i = 0; i < kMaxChildBoxes && reader.remaining() >= 8; ++i) {
    const uint8_t* box_start = reader.current();
    std::optional<BoxHeader> header =
        ReadBoxHeader(&reader, reader.remaining());
    if (!header.has_value()) {
      return;
    }
    const size_t payload_size = header->box_size - header->header_size;
    if (!visitor(header->type, box_start + header->header_size, payload_size)) {
      return;
    }
    reader.Skip(payload_size);
  }
}

// The location of a top-level box payload in the file.
struct BoxLocation {
  size_t payload_offset = 0;
  size_t payload_size = 0;
};

// Finds the first top-level box of type `box_type`. Returns nullopt if there
// is none, or if a malformed box precedes it.
std::optional<BoxLocation> FindTopLevelBox(image_io::DataSource* data_source,
                                           size_t file_size,
                                           std::string_view box_type) {
  size_t position = 0;
  for (size_t i = 0; i < kMaxTopLevelBoxes && file_size - position >= 8; ++i) {
    std::array<uint8_t, 16> header_bytes;
    const size_t header_length =
        std::min(header_bytes.size(), file_size - position);
    if (!ReadFromDataSource(data_source, position, header_length,
                            header_bytes.data())) {
      return std::nullopt;
    }
    BoxReader reader(header_bytes.data(), header_length);
    std::optional<BoxHeader> header =
        ReadBoxHeader(&reader, file_size - position);
    if (!header.has_value()) {
      return std::nullopt;
    }
    if (header->type == box_type) {
      return BoxLocation{position + header->header_size,
                         header->box_size - header->header_size};
    }
    position += header->box_size;
  }
  return std::nullopt;
}

// Returns true if the file has a valid top-level 'mpvd' box, which holds the
// motion photo video in HEIF motion photos. This uses the same check as
// MotionPhotoHeifInfoBuilder.
bool HasMpvdBox(image_io::DataSource* data_source) {
  MpvdBox mpvd_box;
  MpvdBoxScanReceiver receiver(mpvd_box);
  MpvdBox::Scan(data_source, 0, receiver);
  return mpvd_box.IsValid();
}

// A byte range inside the in-memory copy of the 'meta' box payload.
struct ByteRange {
  const uint8_t* data = nullptr;
  size_t size = 0;

  bool found() const { return data != nullptr; }
};

// The 'meta' child boxes used to locate the XMP item.
struct MetaBoxes {
  ByteRange pitm;
  ByteRange iinf;
  ByteRange iref;
  ByteRange iloc;
  ByteRange idat;
};

// Returns the payloads of the first 'pitm', 'iinf', 'iref', 'iloc', and 'idat'
// boxes in a 'meta' box payload.
MetaBoxes FindMetaBoxes(const std::vector<uint8_t>& meta_payload) {
  MetaBoxes boxes;
  BoxReader reader(meta_payload.data(), meta_payload.size());
  // 'meta' is a FullBox: skip its version and flags.
  reader.ReadFullBoxVersion();
  ForEachBox(reader.current(), reader.remaining(),
             [&boxes](std::string_view type, const uint8_t* payload,
                      size_t payload_size) {
               ByteRange* box = nullptr;
               if (type == "pitm") {
                 box = &boxes.pitm;
               } else if (type == "iinf") {
                 box = &boxes.iinf;
               } else if (type == "iref") {
                 box = &boxes.iref;
               } else if (type == "iloc") {
                 box = &boxes.iloc;
               } else if (type == "idat") {
                 box = &boxes.idat;
               }
               if (box != nullptr && !box->found()) {
                 *box = ByteRange{payload, payload_size};
               }
               return true;
             });
  return boxes;
}

// Returns the primary item ID from a 'pitm' payload.
std::optional<uint32_t> ParsePrimaryItemId(ByteRange pitm) {
  BoxReader reader(pitm.data, pitm.size);
  const uint64_t version = reader.ReadFullBoxVersion();
  const uint64_t item_id = reader.ReadUInt(version == 0 ? 2 : 4);
  if (!reader.ok()) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(item_id);
}

// Returns the item ID from an 'infe' payload if the item holds XMP.
std::optional<uint32_t> ParseXmpItemId(const uint8_t* data, size_t size) {
  BoxReader reader(data, size);
  const uint64_t version = reader.ReadFullBoxVersion();
  if (version > 3) {
    return std::nullopt;
  }
  uint64_t item_id = 0;
  if (version >= 2) {
    item_id = reader.ReadUInt(version == 2 ? 2 : 4);
    reader.Skip(2);  // item_protection_index
    if (reader.ReadFourCc() != "mime") {
      return std::nullopt;
    }
  } else {
    item_id = reader.ReadUInt(2);
    reader.Skip(2);  // item_protection_index
  }
  reader.ReadCString();  // item_name
  const std::string_view content_type = reader.ReadCString();
  if (!reader.ok() || content_type != kXmpContentType) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(item_id);
}

// Returns the IDs of the XMP items listed in an 'iinf' payload, in order.
std::vector<uint32_t> FindXmpItemIds(ByteRange iinf) {
  std::vector<uint32_t> xmp_item_ids;
  BoxReader reader(iinf.data, iinf.size);
  const uint64_t version = reader.ReadFullBoxVersion();
  // Skip entry_count and walk the 'infe' boxes directly.
  reader.Skip(version == 0 ? 2 : 4);
  if (!reader.ok()) {
    return xmp_item_ids;
  }
  ForEachBox(reader.current(), reader.remaining(),
             [&xmp_item_ids](std::string_view type, const uint8_t* payload,
                             size_t payload_size) {
               if (type == "infe") {
                 std::optional<uint32_t> item_id =
                     ParseXmpItemId(payload, payload_size);
                 if (item_id.has_value()) {
                   xmp_item_ids.push_back(*item_id);
                 }
               }
               return xmp_item_ids.size() < kMaxXmpItems;
             });
  return xmp_item_ids;
}

// Returns the IDs of the items that have a 'cdsc' (content describes) reference
// to `described_item_id` in an 'iref' payload.
std::vector<uint32_t> FindItemsDescribing(ByteRange iref,
                                          uint32_t described_item_id) {
  std::vector<uint32_t> item_ids;
  BoxReader reader(iref.data, iref.size);
  const size_t item_id_size = reader.ReadFullBoxVersion() == 0 ? 2 : 4;
  if (!reader.ok()) {
    return item_ids;
  }
  ForEachBox(
      reader.current(), reader.remaining(),
      [&](std::string_view type, const uint8_t* payload, size_t payload_size) {
        if (type != "cdsc") {
          return true;
        }
        BoxReader reference(payload, payload_size);
        const uint64_t from_item_id = reference.ReadUInt(item_id_size);
        const uint64_t reference_count = reference.ReadUInt(2);
        for (uint64_t i = 0; i < reference_count; ++i) {
          const uint64_t to_item_id = reference.ReadUInt(item_id_size);
          if (!reference.ok()) {
            break;
          }
          if (to_item_id == described_item_id) {
            item_ids.push_back(static_cast<uint32_t>(from_item_id));
            break;
          }
        }
        return true;
      });
  return item_ids;
}

// Returns the XMP item that describes the primary item, falling back to the
// first XMP item when no 'cdsc' reference links one to the primary item.
uint32_t ChooseXmpItemId(const MetaBoxes& boxes,
                         const std::vector<uint32_t>& xmp_item_ids) {
  if (boxes.pitm.found() && boxes.iref.found()) {
    std::optional<uint32_t> primary_item_id = ParsePrimaryItemId(boxes.pitm);
    if (primary_item_id.has_value()) {
      const std::vector<uint32_t> primary_descriptors =
          FindItemsDescribing(boxes.iref, *primary_item_id);
      for (uint32_t item_id : xmp_item_ids) {
        if (std::find(primary_descriptors.begin(), primary_descriptors.end(),
                      item_id) != primary_descriptors.end()) {
          return item_id;
        }
      }
    }
  }
  return xmp_item_ids.front();
}

// Where the data of an item is stored, as described by its 'iloc' entry.
struct ItemLocation {
  uint64_t construction_method = 0;
  uint64_t data_reference_index = 0;
  uint64_t base_offset = 0;
  // {offset, length} pairs relative to `base_offset`.
  std::vector<std::pair<uint64_t, uint64_t>> extents;
};

bool IsValidIlocFieldSize(size_t num_bytes) {
  return num_bytes == 0 || num_bytes == 4 || num_bytes == 8;
}

// Returns the location of `item_id` from an 'iloc' payload.
std::optional<ItemLocation> FindItemLocation(ByteRange iloc, uint32_t item_id) {
  BoxReader reader(iloc.data, iloc.size);
  const uint64_t version = reader.ReadFullBoxVersion();
  if (version > 2) {
    return std::nullopt;
  }
  const uint64_t field_sizes = reader.ReadUInt(2);
  const size_t offset_size = (field_sizes >> 12) & 0xF;
  const size_t length_size = (field_sizes >> 8) & 0xF;
  const size_t base_offset_size = (field_sizes >> 4) & 0xF;
  const size_t index_size = version == 0 ? 0 : field_sizes & 0xF;
  if (!IsValidIlocFieldSize(offset_size) ||
      !IsValidIlocFieldSize(length_size) ||
      !IsValidIlocFieldSize(base_offset_size) ||
      !IsValidIlocFieldSize(index_size)) {
    return std::nullopt;
  }
  const size_t extent_size = index_size + offset_size + length_size;
  const size_t item_id_size = version < 2 ? 2 : 4;
  const uint64_t item_count = reader.ReadUInt(item_id_size);
  for (uint64_t i = 0; i < item_count && reader.ok(); ++i) {
    const uint64_t current_item_id = reader.ReadUInt(item_id_size);
    ItemLocation location;
    if (version > 0) {
      location.construction_method = reader.ReadUInt(2) & 0xF;
    }
    location.data_reference_index = reader.ReadUInt(2);
    location.base_offset = reader.ReadUInt(base_offset_size);
    const uint64_t extent_count = reader.ReadUInt(2);
    if (current_item_id != item_id) {
      reader.Skip(static_cast<size_t>(extent_count) * extent_size);
      continue;
    }
    if (extent_count > kMaxItemExtents) {
      return std::nullopt;
    }
    for (uint64_t e = 0; e < extent_count; ++e) {
      reader.Skip(index_size);
      const uint64_t extent_offset = reader.ReadUInt(offset_size);
      const uint64_t extent_length = reader.ReadUInt(length_size);
      location.extents.emplace_back(extent_offset, extent_length);
    }
    if (!reader.ok()) {
      return std::nullopt;
    }
    return location;
  }
  return std::nullopt;
}

// Copies the data of an item into `out` by following its extents. Extents are
// file offsets for construction method 0 and offsets into the 'idat' payload
// for construction method 1. Returns the file offset of the first extent, or
// nullopt if the item is stored in an unsupported way, points outside the
// file, or is larger than kMaxXmpItemSize.
std::optional<size_t> ReadItemData(image_io::DataSource* data_source,
                                   size_t file_size,
                                   const ItemLocation& location, ByteRange idat,
                                   size_t idat_file_offset,
                                   std::vector<uint8_t>* out) {
  // A non-zero data_reference_index points at data in another file.
  if (location.data_reference_index != 0 || location.construction_method > 1 ||
      location.extents.empty()) {
    return std::nullopt;
  }
  std::optional<size_t> first_extent_offset;
  for (const auto& [extent_offset, extent_length] : location.extents) {
    // A zero length means "to the end of the data", which XMP never needs.
    if (extent_length == 0 || extent_length > kMaxXmpItemSize - out->size() ||
        extent_offset >
            std::numeric_limits<uint64_t>::max() - location.base_offset) {
      return std::nullopt;
    }
    const uint64_t start = location.base_offset + extent_offset;
    size_t extent_file_offset = 0;
    if (location.construction_method == 0) {
      if (start > file_size || extent_length > file_size - start) {
        return std::nullopt;
      }
      extent_file_offset = static_cast<size_t>(start);
      if (!AppendFromDataSource(data_source, extent_file_offset,
                                static_cast<size_t>(extent_length), out)) {
        return std::nullopt;
      }
    } else {
      if (!idat.found() || start > idat.size ||
          extent_length > idat.size - start) {
        return std::nullopt;
      }
      const uint8_t* extent_data = idat.data + static_cast<size_t>(start);
      out->insert(out->end(), extent_data,
                  extent_data + static_cast<size_t>(extent_length));
      extent_file_offset = idat_file_offset + static_cast<size_t>(start);
    }
    if (!first_extent_offset.has_value()) {
      first_extent_offset = extent_file_offset;
    }
  }
  return first_extent_offset;
}

// Returns the XMP of the primary image as a "standard.xmp" block.
std::optional<RawMetadataBlock> ExtractPrimaryXmp(
    image_io::DataSource* data_source, size_t file_size) {
  std::optional<BoxLocation> meta =
      FindTopLevelBox(data_source, file_size, "meta");
  if (!meta.has_value() || meta->payload_size > kMaxMetaPayloadSize) {
    return std::nullopt;
  }
  std::vector<uint8_t> meta_payload;
  if (!AppendFromDataSource(data_source, meta->payload_offset,
                            meta->payload_size, &meta_payload)) {
    return std::nullopt;
  }
  const MetaBoxes boxes = FindMetaBoxes(meta_payload);
  if (!boxes.iinf.found() || !boxes.iloc.found()) {
    return std::nullopt;
  }
  const std::vector<uint32_t> xmp_item_ids = FindXmpItemIds(boxes.iinf);
  if (xmp_item_ids.empty()) {
    return std::nullopt;
  }
  std::optional<ItemLocation> location =
      FindItemLocation(boxes.iloc, ChooseXmpItemId(boxes, xmp_item_ids));
  if (!location.has_value()) {
    return std::nullopt;
  }
  const size_t idat_file_offset =
      boxes.idat.found()
          ? meta->payload_offset +
                static_cast<size_t>(boxes.idat.data - meta_payload.data())
          : 0;

  RawMetadataBlock block;
  block.type = "XMP";
  block.format_identifier = "standard.xmp";
  std::optional<size_t> offset =
      ReadItemData(data_source, file_size, *location, boxes.idat,
                   idat_file_offset, &block.bytes);
  if (!offset.has_value()) {
    return std::nullopt;
  }
  block.offset = *offset;
  return block;
}

class HeifContainerSplitter : public ContainerSplitter {
 public:
  // Emits the primary image's XMP only when the file also has an 'mpvd' box:
  // editors often drop the unknown 'mpvd' box but keep the XMP, and such a file
  // no longer contains a video. The video itself is not copied.
  std::vector<RawMetadataBlock> Split(image_io::DataSource* data_source,
                                      size_t file_size) override {
    std::vector<RawMetadataBlock> blocks;
    if (data_source == nullptr || file_size == 0 || !HasMpvdBox(data_source)) {
      return blocks;
    }
    std::optional<RawMetadataBlock> xmp_block =
        ExtractPrimaryXmp(data_source, file_size);
    if (xmp_block.has_value()) {
      blocks.push_back(*std::move(xmp_block));
    }
    return blocks;
  }
};

}  // namespace

std::unique_ptr<ContainerSplitter> CreateHeifContainerSplitter() {
  return std::make_unique<HeifContainerSplitter>();
}

}  // namespace motion_photo
}  // namespace libmotionphoto
