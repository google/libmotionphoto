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
#include "motion_photo/metadata_value.h"
#include "motion_photo/motion_photo_utils.h"
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

// Appends `length` bytes at file offset `start` to `out`.
bool AppendFromDataSource(image_io::DataSource* data_source, size_t start,
                          size_t length, std::vector<uint8_t>* out) {
  if (out == nullptr) return false;
  const size_t old_size = out->size();
  out->resize(old_size + length);
  return ReadDataRange(data_source, start, length, out->data() + old_size);
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
    if (!ReadDataRange(data_source, position, header_length,
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

// Returns the item ID from an 'infe' payload if the item holds uncompressed XMP.
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
  if (!reader.ok() || !EqualsIgnoreCase(content_type, kXmpContentType)) {
    return std::nullopt;
  }
  if (reader.remaining() > 0) {
    const std::string_view content_encoding = reader.ReadCString();
    if (!reader.ok() || !content_encoding.empty()) {
      return std::nullopt;
    }
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

// Scans an 'iref' payload for 'cdsc' (content describes) references. Returns
// true if any 'cdsc' reference box was present, and appends all item IDs that
// describe `described_item_id` into `out_descriptors`.
bool FindCdscDescriptors(ByteRange iref, uint32_t described_item_id,
                         std::vector<uint32_t>* out_descriptors) {
  bool has_any_cdsc = false;
  BoxReader reader(iref.data, iref.size);
  const size_t item_id_size = reader.ReadFullBoxVersion() == 0 ? 2 : 4;
  if (!reader.ok()) {
    return false;
  }
  ForEachBox(
      reader.current(), reader.remaining(),
      [&](std::string_view type, const uint8_t* payload, size_t payload_size) {
        if (type != "cdsc") {
          return true;
        }
        has_any_cdsc = true;
        BoxReader reference(payload, payload_size);
        const uint64_t from_item_id = reference.ReadUInt(item_id_size);
        const uint64_t reference_count = reference.ReadUInt(2);
        for (uint64_t i = 0; i < reference_count; ++i) {
          const uint64_t to_item_id = reference.ReadUInt(item_id_size);
          if (!reference.ok()) {
            break;
          }
          if (to_item_id == described_item_id) {
            out_descriptors->push_back(static_cast<uint32_t>(from_item_id));
            break;
          }
        }
        return true;
      });
  return has_any_cdsc;
}

// Returns the XMP item that describes the primary item. Only falls back to the
// first XMP item when 'iref' is absent or contains zero 'cdsc' references; if
// 'cdsc' references exist (e.g. a GainMap's hdrgm XMP) and none describes the
// primary item, returns std::nullopt instead of misattributing another item's
// XMP.
std::optional<uint32_t> ChooseXmpItemId(
    const MetaBoxes& boxes, const std::vector<uint32_t>& xmp_item_ids) {
  if (boxes.iref.found()) {
    std::optional<uint32_t> primary_item_id =
        boxes.pitm.found() ? ParsePrimaryItemId(boxes.pitm) : std::nullopt;
    std::vector<uint32_t> primary_descriptors;
    const bool has_any_cdsc = FindCdscDescriptors(
        boxes.iref, primary_item_id.value_or(0), &primary_descriptors);
    if (primary_item_id.has_value()) {
      for (uint32_t item_id : xmp_item_ids) {
        if (std::find(primary_descriptors.begin(), primary_descriptors.end(),
                      item_id) != primary_descriptors.end()) {
          return item_id;
        }
      }
    }
    if (has_any_cdsc) {
      return std::nullopt;
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

// Resolves the file-coordinate DataRanges for an item's 'iloc' extents and,
// if `out_bytes` is non-null, appends the item's payload bytes into it.
bool ResolveAndReadItemExtents(image_io::DataSource* data_source,
                               size_t file_size, const ItemLocation& location,
                               ByteRange idat, size_t idat_file_offset,
                               std::vector<image_io::DataRange>* out_ranges,
                               std::vector<uint8_t>* out_bytes) {
  // A non-zero data_reference_index points at data in another file.
  if (location.data_reference_index != 0 || location.construction_method > 1 ||
      location.extents.empty()) {
    return false;
  }
  size_t total_size = 0;
  for (const auto& [extent_offset, extent_length] : location.extents) {
    // A zero length means "to the end of the data", which XMP never needs.
    if (extent_length == 0 || extent_length > kMaxXmpItemSize - total_size ||
        extent_offset >
            std::numeric_limits<uint64_t>::max() - location.base_offset) {
      return false;
    }
    const uint64_t start = location.base_offset + extent_offset;
    size_t extent_file_offset = 0;
    const size_t len = static_cast<size_t>(extent_length);
    if (location.construction_method == 0) {
      if (start > file_size || len > file_size - start) {
        return false;
      }
      extent_file_offset = static_cast<size_t>(start);
      if (out_bytes != nullptr &&
          !AppendFromDataSource(data_source, extent_file_offset, len,
                                out_bytes)) {
        return false;
      }
    } else {
      if (!idat.found() || start > idat.size || len > idat.size - start) {
        return false;
      }
      if (out_bytes != nullptr) {
        const uint8_t* extent_data = idat.data + static_cast<size_t>(start);
        out_bytes->insert(out_bytes->end(), extent_data, extent_data + len);
      }
      extent_file_offset = idat_file_offset + static_cast<size_t>(start);
    }
    total_size += len;
    if (out_ranges != nullptr) {
      const size_t extent_end = extent_file_offset + len;
      if (!out_ranges->empty() &&
          out_ranges->back().GetEnd() == extent_file_offset) {
        out_ranges->back() =
            image_io::DataRange(out_ranges->back().GetBegin(), extent_end);
      } else {
        out_ranges->emplace_back(extent_file_offset, extent_end);
      }
    }
  }
  return total_size > 0;
}

// Extracts the primary image's XMP extents (and optionally payload bytes).
std::optional<RawMetadataBlock> ExtractPrimaryXmp(
    image_io::DataSource* data_source, size_t file_size,
    std::vector<image_io::DataRange>* out_ranges = nullptr,
    bool read_bytes = true) {
  if (out_ranges != nullptr) out_ranges->clear();
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
  const std::optional<uint32_t> chosen_id = ChooseXmpItemId(boxes, xmp_item_ids);
  if (!chosen_id.has_value()) {
    return std::nullopt;
  }
  std::optional<ItemLocation> location =
      FindItemLocation(boxes.iloc, *chosen_id);
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
  std::vector<image_io::DataRange> local_ranges;
  std::vector<image_io::DataRange>* ranges_ptr =
      out_ranges != nullptr ? out_ranges : &local_ranges;
  if (!ResolveAndReadItemExtents(data_source, file_size, *location, boxes.idat,
                                 idat_file_offset, ranges_ptr,
                                 read_bytes ? &block.bytes : nullptr) ||
      ranges_ptr->empty()) {
    if (out_ranges != nullptr) out_ranges->clear();
    return std::nullopt;
  }
  block.offset = ranges_ptr->front().GetBegin();
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
    if (data_source == nullptr || file_size == 0) {
      return blocks;
    }
    std::optional<BoxLocation> mpvd =
        FindTopLevelBox(data_source, file_size, "mpvd");
    if (mpvd.has_value()) {
      std::optional<RawMetadataBlock> xmp_block =
          ExtractPrimaryXmp(data_source, file_size);
      if (xmp_block.has_value()) {
        blocks.push_back(*std::move(xmp_block));
      }
    }
    std::optional<BoxLocation> sefd =
        FindTopLevelBox(data_source, file_size, "sefd");
    if (sefd.has_value() && sefd->payload_size > 0 &&
        sefd->payload_size <= kMaxMetaPayloadSize) {
      RawMetadataBlock sefd_block;
      sefd_block.type = "SEFD";
      sefd_block.format_identifier = "heic.sefd";
      sefd_block.offset =
          mpvd.has_value() ? mpvd->payload_offset : sefd->payload_offset;
      sefd_block.total_payload_size =
          mpvd.has_value() ? mpvd->payload_size : sefd->payload_size;
      if (AppendFromDataSource(data_source, sefd->payload_offset,
                               sefd->payload_size, &sefd_block.bytes)) {
        blocks.push_back(std::move(sefd_block));
      }
    }
    return blocks;
  }
};

}  // namespace

std::unique_ptr<ContainerSplitter> CreateHeifContainerSplitter() {
  return std::make_unique<HeifContainerSplitter>();
}

std::vector<image_io::DataRange> FindHeifPrimaryXmpRanges(
    image_io::DataSource* data_source, size_t file_size) {
  std::vector<image_io::DataRange> ranges;
  if (data_source == nullptr || file_size == 0) {
    return ranges;
  }
  if (!ExtractPrimaryXmp(data_source, file_size, &ranges,
                         /*read_bytes=*/false)
           .has_value()) {
    ranges.clear();
  }
  return ranges;
}

image_io::DataRange FindHeifPrimaryXmpRange(image_io::DataSource* data_source,
                                            size_t file_size) {
  std::vector<image_io::DataRange> ranges =
      FindHeifPrimaryXmpRanges(data_source, file_size);
  if (ranges.size() != 1) {
    return image_io::DataRange();
  }
  return ranges.front();
}

}  // namespace motion_photo
}  // namespace libmotionphoto
