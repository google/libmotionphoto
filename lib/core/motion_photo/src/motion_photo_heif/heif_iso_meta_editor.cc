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

#include "motion_photo_heif/heif_iso_meta_editor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace libmotionphoto {
namespace motion_photo {

namespace {

constexpr std::string_view kXmpContentType = "application/rdf+xml";

constexpr uint32_t FourCc(const char (&s)[5]) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
}

constexpr uint32_t kFtyp = FourCc("ftyp");
constexpr uint32_t kMeta = FourCc("meta");
constexpr uint32_t kMoov = FourCc("moov");
constexpr uint32_t kMdat = FourCc("mdat");
constexpr uint32_t kMpvd = FourCc("mpvd");
constexpr uint32_t kSefd = FourCc("sefd");
constexpr uint32_t kPitm = FourCc("pitm");
constexpr uint32_t kIinf = FourCc("iinf");
constexpr uint32_t kInfe = FourCc("infe");
constexpr uint32_t kIloc = FourCc("iloc");
constexpr uint32_t kIref = FourCc("iref");
constexpr uint32_t kCdsc = FourCc("cdsc");
constexpr uint32_t kMime = FourCc("mime");

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

// Bounds-checked big-endian reader over [data, data + size).
class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool ReadUint(size_t num_bytes, uint64_t* value) {
    if (num_bytes > 8 || num_bytes > size_ - pos_) return false;
    uint64_t v = 0;
    for (size_t i = 0; i < num_bytes; ++i) v = (v << 8) | data_[pos_ + i];
    pos_ += num_bytes;
    *value = v;
    return true;
  }
  bool Skip(size_t num_bytes) {
    if (num_bytes > size_ - pos_) return false;
    pos_ += num_bytes;
    return true;
  }
  // Reads a NUL-terminated string (without the terminator).
  bool ReadCString(std::string_view* value) {
    const void* nul = std::memchr(data_ + pos_, 0, size_ - pos_);
    if (nul == nullptr) return false;
    size_t len = static_cast<const uint8_t*>(nul) - (data_ + pos_);
    *value = std::string_view(reinterpret_cast<const char*>(data_ + pos_), len);
    pos_ += len + 1;
    return true;
  }
  size_t pos() const { return pos_; }
  size_t remaining() const { return size_ - pos_; }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
};

void WriteUint(std::vector<uint8_t>* out, uint64_t value, size_t num_bytes) {
  for (size_t i = num_bytes; i > 0; --i) {
    out->push_back(static_cast<uint8_t>(value >> (8 * (i - 1))));
  }
}

void PatchUint32(std::vector<uint8_t>* out, size_t pos, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) {
    (*out)[pos + i] = static_cast<uint8_t>(value >> (8 * (3 - i)));
  }
}

bool FitsIn(uint64_t value, size_t num_bytes) {
  return num_bytes >= 8 || value < (uint64_t{1} << (8 * num_bytes));
}

struct Box {
  size_t offset = 0;      // Absolute offset of the box header.
  size_t size = 0;        // Total box size, including the header.
  size_t header = 0;      // Header length (8, or 16 for a 64-bit size).
  uint32_t type = 0;
  bool size_was_zero = false;  // The box size field was 0 ("to end of file").

  size_t end() const { return offset + size; }
};

// Parses the sequence of boxes inside [begin, end) of `data`.
bool ParseBoxes(const uint8_t* data, size_t begin, size_t end,
                std::vector<Box>* boxes) {
  size_t pos = begin;
  while (pos < end) {
    Reader reader(data + pos, end - pos);
    uint64_t size = 0, type = 0;
    if (!reader.ReadUint(4, &size) || !reader.ReadUint(4, &type)) return false;
    Box box;
    box.offset = pos;
    box.type = static_cast<uint32_t>(type);
    box.header = 8;
    if (size == 1) {
      if (!reader.ReadUint(8, &size)) return false;
      box.header = 16;
    } else if (size == 0) {
      size = end - pos;
      box.size_was_zero = true;
    }
    if (size < box.header || size > end - pos) return false;
    box.size = static_cast<size_t>(size);
    boxes->push_back(box);
    pos += box.size;
  }
  return true;
}

// ---------------------------------------------------------------------------
// 'iloc' (ISO/IEC 14496-12 8.11.3)
// ---------------------------------------------------------------------------
struct IlocExtent {
  uint64_t index = 0;
  uint64_t offset = 0;  // base_offset already folded in.
  uint64_t length = 0;
};

struct IlocItem {
  uint32_t id = 0;
  uint8_t construction_method = 0;
  uint16_t data_reference_index = 0;
  std::vector<IlocExtent> extents;

  // True if the extents are absolute offsets into this file.
  bool IsFileOffset() const {
    return construction_method == 0 && data_reference_index == 0;
  }
};

struct Iloc {
  uint8_t version = 0;
  uint32_t flags = 0;
  uint8_t index_size = 0;
  std::vector<IlocItem> items;
};

bool IsValidFieldSize(uint64_t size) {
  return size == 0 || size == 4 || size == 8;
}

bool ParseIloc(const uint8_t* payload, size_t size, Iloc* iloc) {
  Reader r(payload, size);
  uint64_t version, flags, sizes1, sizes2;
  if (!r.ReadUint(1, &version) || !r.ReadUint(3, &flags) ||
      !r.ReadUint(1, &sizes1) || !r.ReadUint(1, &sizes2) || version > 2) {
    return false;
  }
  iloc->version = static_cast<uint8_t>(version);
  iloc->flags = static_cast<uint32_t>(flags);
  const size_t offset_size = sizes1 >> 4;
  const size_t length_size = sizes1 & 0xF;
  const size_t base_offset_size = sizes2 >> 4;
  const size_t index_size = version >= 1 ? (sizes2 & 0xF) : 0;
  if (!IsValidFieldSize(offset_size) || !IsValidFieldSize(length_size) ||
      !IsValidFieldSize(base_offset_size) || !IsValidFieldSize(index_size)) {
    return false;
  }
  iloc->index_size = static_cast<uint8_t>(index_size);
  const size_t id_size = version < 2 ? 2 : 4;
  uint64_t item_count;
  if (!r.ReadUint(id_size, &item_count)) return false;
  // Each item needs at least 6 bytes, which bounds hostile item counts.
  if (item_count > r.remaining() / 6) return false;
  iloc->items.reserve(item_count);
  for (uint64_t i = 0; i < item_count; ++i) {
    IlocItem item;
    uint64_t id, value, base_offset = 0, extent_count;
    if (!r.ReadUint(id_size, &id)) return false;
    item.id = static_cast<uint32_t>(id);
    if (version >= 1) {
      if (!r.ReadUint(2, &value)) return false;
      item.construction_method = static_cast<uint8_t>(value & 0xF);
    }
    if (!r.ReadUint(2, &value)) return false;
    item.data_reference_index = static_cast<uint16_t>(value);
    if (!r.ReadUint(base_offset_size, &base_offset) ||
        !r.ReadUint(2, &extent_count)) {
      return false;
    }
    const size_t extent_bytes = index_size + offset_size + length_size;
    if (extent_bytes > 0 && extent_count > r.remaining() / extent_bytes) {
      return false;
    }
    for (uint64_t e = 0; e < extent_count; ++e) {
      IlocExtent extent;
      uint64_t offset = 0;
      if (!r.ReadUint(index_size, &extent.index) ||
          !r.ReadUint(offset_size, &offset) ||
          !r.ReadUint(length_size, &extent.length)) {
        return false;
      }
      if (offset > std::numeric_limits<uint64_t>::max() - base_offset) {
        return false;
      }
      extent.offset = base_offset + offset;
      item.extents.push_back(extent);
    }
    iloc->items.push_back(std::move(item));
  }
  return true;
}

// Serializes `iloc` as a complete 'iloc' box with base_offset_size 0 and the
// given offset/length field sizes.
std::vector<uint8_t> SerializeIloc(const Iloc& iloc, size_t offset_size,
                                   size_t length_size) {
  std::vector<uint8_t> out;
  WriteUint(&out, 0, 4);  // Size, patched below.
  WriteUint(&out, kIloc, 4);
  WriteUint(&out, iloc.version, 1);
  WriteUint(&out, iloc.flags, 3);
  WriteUint(&out, (offset_size << 4) | length_size, 1);
  WriteUint(&out, iloc.version >= 1 ? iloc.index_size : 0, 1);
  const size_t id_size = iloc.version < 2 ? 2 : 4;
  WriteUint(&out, iloc.items.size(), id_size);
  for (const IlocItem& item : iloc.items) {
    WriteUint(&out, item.id, id_size);
    if (iloc.version >= 1) WriteUint(&out, item.construction_method, 2);
    WriteUint(&out, item.data_reference_index, 2);
    WriteUint(&out, item.extents.size(), 2);
    for (const IlocExtent& extent : item.extents) {
      if (iloc.version >= 1) WriteUint(&out, extent.index, iloc.index_size);
      WriteUint(&out, extent.offset, offset_size);
      WriteUint(&out, extent.length, length_size);
    }
  }
  PatchUint32(&out, 0, static_cast<uint32_t>(out.size()));
  return out;
}

// ---------------------------------------------------------------------------
// 'iinf' / 'infe' (ISO/IEC 14496-12 8.11.6)
// ---------------------------------------------------------------------------
struct InfeEntry {
  uint32_t id = 0;
  uint32_t item_type = 0;
  std::string_view content_type;
  size_t offset = 0;  // Absolute offset of the 'infe' box.
  size_t size = 0;
};

struct Iinf {
  uint8_t version = 0;
  uint32_t flags = 0;
  std::vector<InfeEntry> entries;
};

bool ParseIinf(const uint8_t* data, const Box& box, Iinf* iinf) {
  Reader r(data + box.offset + box.header, box.size - box.header);
  uint64_t version, flags, count;
  if (!r.ReadUint(1, &version) || !r.ReadUint(3, &flags) ||
      !r.ReadUint(version == 0 ? 2 : 4, &count)) {
    return false;
  }
  iinf->version = static_cast<uint8_t>(version);
  iinf->flags = static_cast<uint32_t>(flags);
  std::vector<Box> infe_boxes;
  const size_t children_begin = box.offset + box.header + r.pos();
  if (!ParseBoxes(data, children_begin, box.end(), &infe_boxes)) return false;
  for (const Box& infe : infe_boxes) {
    if (infe.type != kInfe) continue;
    InfeEntry entry;
    entry.offset = infe.offset;
    entry.size = infe.size;
    Reader e(data + infe.offset + infe.header, infe.size - infe.header);
    uint64_t infe_version, infe_flags, id, protection;
    if (!e.ReadUint(1, &infe_version) || !e.ReadUint(3, &infe_flags) ||
        !e.ReadUint(infe_version == 3 ? 4 : 2, &id) ||
        !e.ReadUint(2, &protection)) {
      return false;
    }
    entry.id = static_cast<uint32_t>(id);
    if (infe_version >= 2) {
      uint64_t item_type;
      std::string_view name;
      if (!e.ReadUint(4, &item_type) || !e.ReadCString(&name)) return false;
      entry.item_type = static_cast<uint32_t>(item_type);
      if (entry.item_type == kMime && !e.ReadCString(&entry.content_type)) {
        return false;
      }
    }
    iinf->entries.push_back(entry);
  }
  if (iinf->entries.size() != count) return false;
  return true;
}

std::vector<uint8_t> SerializeXmpInfe(uint32_t id) {
  std::vector<uint8_t> out;
  const uint8_t version = id > 0xFFFF ? 3 : 2;
  WriteUint(&out, 0, 4);
  WriteUint(&out, kInfe, 4);
  WriteUint(&out, version, 1);
  WriteUint(&out, 1, 3);  // flags: hidden (metadata item).
  WriteUint(&out, id, version == 3 ? 4 : 2);
  WriteUint(&out, 0, 2);  // item_protection_index
  WriteUint(&out, kMime, 4);
  out.push_back(0);  // item_name: empty
  out.insert(out.end(), kXmpContentType.begin(), kXmpContentType.end());
  out.push_back(0);
  PatchUint32(&out, 0, static_cast<uint32_t>(out.size()));
  return out;
}

// ---------------------------------------------------------------------------
// 'iref' (ISO/IEC 14496-12 8.11.12)
// ---------------------------------------------------------------------------
struct ItemReference {
  uint32_t type = 0;
  uint32_t from = 0;
  std::vector<uint32_t> to;
};

struct Iref {
  uint8_t version = 0;
  uint32_t flags = 0;
  std::vector<ItemReference> references;
};

bool ParseIref(const uint8_t* data, const Box& box, Iref* iref) {
  Reader r(data + box.offset + box.header, box.size - box.header);
  uint64_t version, flags;
  if (!r.ReadUint(1, &version) || !r.ReadUint(3, &flags) || version > 1) {
    return false;
  }
  iref->version = static_cast<uint8_t>(version);
  iref->flags = static_cast<uint32_t>(flags);
  const size_t id_size = version == 0 ? 2 : 4;
  std::vector<Box> children;
  if (!ParseBoxes(data, box.offset + box.header + r.pos(), box.end(),
                  &children)) {
    return false;
  }
  for (const Box& child : children) {
    Reader c(data + child.offset + child.header, child.size - child.header);
    ItemReference ref;
    ref.type = child.type;
    uint64_t from, count;
    if (!c.ReadUint(id_size, &from) || !c.ReadUint(2, &count) ||
        count > c.remaining() / id_size) {
      return false;
    }
    ref.from = static_cast<uint32_t>(from);
    for (uint64_t i = 0; i < count; ++i) {
      uint64_t to;
      if (!c.ReadUint(id_size, &to)) return false;
      ref.to.push_back(static_cast<uint32_t>(to));
    }
    iref->references.push_back(std::move(ref));
  }
  return true;
}

std::vector<uint8_t> SerializeIref(const Iref& iref) {
  bool needs_wide_ids = false;
  for (const ItemReference& ref : iref.references) {
    needs_wide_ids |= ref.from > 0xFFFF;
    for (uint32_t to : ref.to) needs_wide_ids |= to > 0xFFFF;
  }
  const uint8_t version = needs_wide_ids ? 1 : iref.version;
  const size_t id_size = version == 0 ? 2 : 4;
  std::vector<uint8_t> out;
  WriteUint(&out, 0, 4);
  WriteUint(&out, kIref, 4);
  WriteUint(&out, version, 1);
  WriteUint(&out, iref.flags, 3);
  for (const ItemReference& ref : iref.references) {
    const size_t child_size = 8 + id_size + 2 + id_size * ref.to.size();
    WriteUint(&out, child_size, 4);
    WriteUint(&out, ref.type, 4);
    WriteUint(&out, ref.from, id_size);
    WriteUint(&out, ref.to.size(), 2);
    for (uint32_t to : ref.to) WriteUint(&out, to, id_size);
  }
  PatchUint32(&out, 0, static_cast<uint32_t>(out.size()));
  return out;
}

bool ParsePitm(const uint8_t* data, const Box& box, uint32_t* primary_id) {
  Reader r(data + box.offset + box.header, box.size - box.header);
  uint64_t version, flags, id;
  if (!r.ReadUint(1, &version) || !r.ReadUint(3, &flags) ||
      !r.ReadUint(version == 0 ? 2 : 4, &id)) {
    return false;
  }
  *primary_id = static_cast<uint32_t>(id);
  return true;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

// Returns the top-level box fully containing [offset, offset + length), or
// nullptr if no single box does.
const Box* FindContainingBox(const std::vector<Box>& boxes, uint64_t offset,
                             uint64_t length) {
  for (const Box& box : boxes) {
    if (offset >= box.offset && offset <= box.end() &&
        length <= box.end() - offset) {
      return &box;
    }
  }
  return nullptr;
}

bool ExtentsOverlap(const IlocExtent& a, const IlocExtent& b) {
  return a.offset < b.offset + b.length && b.offset < a.offset + a.length;
}

// True if `extent` overlaps any file-offset extent of an item other than
// `item_id`, i.e. the bytes are shared and must not be modified.
bool IsShared(const Iloc& iloc, uint32_t item_id, const IlocExtent& extent) {
  for (const IlocItem& item : iloc.items) {
    if (item.id == item_id || !item.IsFileOffset()) continue;
    for (const IlocExtent& other : item.extents) {
      if (ExtentsOverlap(extent, other)) return true;
    }
  }
  return false;
}

}  // namespace

bool HeifIsoMetaEditor::SetPrimaryXmp(const uint8_t* data, size_t size,
                                      std::string_view xmp,
                                      std::vector<uint8_t>* output,
                                      std::string* error) {
  if (data == nullptr || output == nullptr || xmp.empty()) {
    return Fail(error, "Invalid arguments");
  }

  // 1. Top-level layout.
  std::vector<Box> top;
  if (!ParseBoxes(data, 0, size, &top) || top.empty() ||
      top.front().type != kFtyp) {
    return Fail(error, "Not an ISO-BMFF file starting with 'ftyp'");
  }
  std::vector<Box> kept;
  std::optional<size_t> meta_index;
  bool has_moov = false;
  bool removed_box_before_kept_box = false;
  bool removed_any = false;
  for (const Box& box : top) {
    if (box.type == kMpvd || box.type == kSefd) {
      removed_any = true;
      continue;
    }
    if (removed_any) removed_box_before_kept_box = true;
    if (box.type == kMoov) has_moov = true;
    if (box.type == kMeta) {
      if (meta_index.has_value()) {
        return Fail(error, "Multiple top-level 'meta' boxes");
      }
      meta_index = kept.size();
    }
    kept.push_back(box);
  }
  if (!meta_index.has_value()) return Fail(error, "No top-level 'meta' box");
  const Box meta_box = kept[*meta_index];

  // 2. 'meta' children.
  if (meta_box.size - meta_box.header < 4) return Fail(error, "Bad 'meta' box");
  const size_t meta_children_begin = meta_box.offset + meta_box.header + 4;
  std::vector<Box> meta_children;
  if (!ParseBoxes(data, meta_children_begin, meta_box.end(), &meta_children)) {
    return Fail(error, "Malformed 'meta' children");
  }
  const Box* pitm_box = nullptr;
  const Box* iinf_box = nullptr;
  const Box* iloc_box = nullptr;
  const Box* iref_box = nullptr;
  for (const Box& child : meta_children) {
    if (child.type == kPitm && pitm_box == nullptr) pitm_box = &child;
    if (child.type == kIinf && iinf_box == nullptr) iinf_box = &child;
    if (child.type == kIloc && iloc_box == nullptr) iloc_box = &child;
    if (child.type == kIref && iref_box == nullptr) iref_box = &child;
  }
  if (pitm_box == nullptr || iinf_box == nullptr || iloc_box == nullptr) {
    return Fail(error, "'meta' is missing 'pitm', 'iinf' or 'iloc'");
  }

  uint32_t primary_id = 0;
  Iinf iinf;
  Iloc iloc;
  Iref iref;
  if (!ParsePitm(data, *pitm_box, &primary_id) ||
      !ParseIinf(data, *iinf_box, &iinf) ||
      !ParseIloc(data + iloc_box->offset + iloc_box->header,
                 iloc_box->size - iloc_box->header, &iloc) ||
      (iref_box != nullptr && !ParseIref(data, *iref_box, &iref))) {
    return Fail(error, "Malformed 'pitm', 'iinf', 'iloc' or 'iref'");
  }

  // 3. Locate the XMP item describing the primary image.
  std::optional<uint32_t> xmp_id;
  uint32_t max_id = 0;
  for (const InfeEntry& entry : iinf.entries) {
    max_id = std::max(max_id, entry.id);
    if (xmp_id.has_value() || entry.item_type != kMime ||
        !EqualsIgnoreCase(entry.content_type, kXmpContentType)) {
      continue;
    }
    for (const ItemReference& ref : iref.references) {
      if (ref.type == kCdsc && ref.from == entry.id &&
          std::find(ref.to.begin(), ref.to.end(), primary_id) != ref.to.end()) {
        xmp_id = entry.id;
        break;
      }
    }
  }
  for (const IlocItem& item : iloc.items) max_id = std::max(max_id, item.id);
  IlocItem* xmp_item = nullptr;
  if (xmp_id.has_value()) {
    for (IlocItem& item : iloc.items) {
      if (item.id == *xmp_id) xmp_item = &item;
    }
    if (xmp_item == nullptr) return Fail(error, "XMP item has no 'iloc' entry");
  }

  // Every file-offset extent must live inside a single kept, non-'meta'
  // top-level box so it can be relocated safely.
  for (const IlocItem& item : iloc.items) {
    if (!item.IsFileOffset() || &item == xmp_item) continue;
    for (const IlocExtent& extent : item.extents) {
      const Box* box = FindContainingBox(kept, extent.offset, extent.length);
      if (extent.length == 0 || box == nullptr || box->type == kMeta) {
        return Fail(error, "Unsupported item extent layout");
      }
    }
  }

  // 4a. Fast path: overwrite the existing XMP bytes in place when the new XMP
  // fits and nothing in front of the image data has to move.
  if (xmp_item != nullptr && xmp_item->IsFileOffset() &&
      xmp_item->extents.size() == 1 && !removed_box_before_kept_box) {
    const IlocExtent& extent = xmp_item->extents[0];
    const Box* box = FindContainingBox(kept, extent.offset, extent.length);
    if (box != nullptr && box->type != kMeta && xmp.size() <= extent.length &&
        !IsShared(iloc, xmp_item->id, extent)) {
      const size_t end = kept.back().end();
      output->assign(data, data + end);
      std::memcpy(output->data() + extent.offset, xmp.data(), xmp.size());
      std::memset(output->data() + extent.offset + xmp.size(), ' ',
                  extent.length - xmp.size());
      if (kept.back().size_was_zero) {
        if (!FitsIn(kept.back().size, 4) || kept.back().header != 8) {
          return Fail(error, "Unsupported trailing box size");
        }
        PatchUint32(output, kept.back().offset,
                    static_cast<uint32_t>(kept.back().size));
      }
      return true;
    }
  }

  // 4b. General path: rebuild 'meta' and append the XMP payload in a new
  // 'mdat' box at the end of the image.
  if (has_moov) {
    return Fail(error, "Image sequences ('moov') are not supported");
  }
  std::optional<IlocExtent> orphaned_xmp_extent;
  if (xmp_item != nullptr && xmp_item->IsFileOffset() &&
      xmp_item->extents.size() == 1) {
    const IlocExtent& extent = xmp_item->extents[0];
    const Box* box = FindContainingBox(kept, extent.offset, extent.length);
    if (box != nullptr && box->type != kMeta &&
        !IsShared(iloc, xmp_item->id, extent)) {
      orphaned_xmp_extent = extent;
    }
  }

  uint32_t new_xmp_id = 0;
  if (xmp_item == nullptr) {
    if (max_id == std::numeric_limits<uint32_t>::max()) {
      return Fail(error, "Item id space exhausted");
    }
    new_xmp_id = max_id + 1;
    IlocItem item;
    item.id = new_xmp_id;
    iloc.items.push_back(item);
    xmp_item = &iloc.items.back();
    ItemReference ref;
    ref.type = kCdsc;
    ref.from = new_xmp_id;
    ref.to.push_back(primary_id);
    iref.references.push_back(ref);
  }
  xmp_item->construction_method = 0;
  xmp_item->data_reference_index = 0;
  xmp_item->extents.assign(1, IlocExtent{});
  xmp_item->extents[0].length = xmp.size();
  if (new_xmp_id > 0xFFFF && iloc.version < 2) iloc.version = 2;

  // New 'iinf' (version 1 when the entry count no longer fits 16 bits).
  std::vector<uint8_t> new_iinf;
  {
    const size_t count = iinf.entries.size() + (new_xmp_id != 0 ? 1 : 0);
    const uint8_t version = count > 0xFFFF ? 1 : iinf.version;
    WriteUint(&new_iinf, 0, 4);
    WriteUint(&new_iinf, kIinf, 4);
    WriteUint(&new_iinf, version, 1);
    WriteUint(&new_iinf, iinf.flags, 3);
    WriteUint(&new_iinf, count, version == 0 ? 2 : 4);
    for (const InfeEntry& entry : iinf.entries) {
      new_iinf.insert(new_iinf.end(), data + entry.offset,
                      data + entry.offset + entry.size);
    }
    if (new_xmp_id != 0) {
      std::vector<uint8_t> infe = SerializeXmpInfe(new_xmp_id);
      new_iinf.insert(new_iinf.end(), infe.begin(), infe.end());
    }
    if (!FitsIn(new_iinf.size(), 4)) return Fail(error, "'iinf' too large");
    PatchUint32(&new_iinf, 0, static_cast<uint32_t>(new_iinf.size()));
  }
  const std::vector<uint8_t> new_iref = SerializeIref(iref);

  // Pick iloc field sizes so the 'meta' size does not depend on offsets.
  uint64_t max_length = xmp.size();
  for (const IlocItem& item : iloc.items) {
    for (const IlocExtent& extent : item.extents) {
      max_length = std::max(max_length, extent.length);
    }
  }
  const size_t length_size = FitsIn(max_length, 4) ? 4 : 8;
  const size_t iloc_size_wide = SerializeIloc(iloc, 8, length_size).size();
  uint64_t meta_upper_bound = meta_box.size + new_iinf.size() + new_iref.size() +
                              iloc_size_wide + 16;
  const uint64_t total_upper_bound =
      static_cast<uint64_t>(size) + meta_upper_bound + xmp.size() + 16;
  const size_t offset_size = FitsIn(total_upper_bound, 4) ? 4 : 8;
  for (const IlocItem& item : iloc.items) {
    if (item.IsFileOffset()) continue;
    for (const IlocExtent& extent : item.extents) {
      if (!FitsIn(extent.offset, offset_size)) {
        return Fail(error, "'iloc' offset does not fit");
      }
    }
  }
  const size_t new_iloc_size = SerializeIloc(iloc, offset_size, length_size).size();

  // New 'meta' size.
  size_t meta_payload = 4;  // version + flags
  for (const Box& child : meta_children) {
    if (child.type == kIinf && &child == iinf_box) {
      meta_payload += new_iinf.size();
    } else if (child.type == kIloc && &child == iloc_box) {
      meta_payload += new_iloc_size;
    } else if (child.type == kIref && &child == iref_box) {
      meta_payload += new_iref.size();
    } else {
      meta_payload += child.size;
    }
  }
  if (iref_box == nullptr) meta_payload += new_iref.size();
  const size_t meta_header = FitsIn(meta_payload + 8, 4) ? 8 : 16;
  const size_t new_meta_size = meta_header + meta_payload;

  // 5. New layout: kept boxes in order (with the rebuilt 'meta'), then the XMP
  // 'mdat'.
  std::vector<size_t> new_offsets(kept.size());
  size_t cursor = 0;
  for (size_t i = 0; i < kept.size(); ++i) {
    new_offsets[i] = cursor;
    const size_t box_size = kept[i].type == kMeta ? new_meta_size : kept[i].size;
    if (kept[i].size_was_zero &&
        (!FitsIn(kept[i].size, 4) || kept[i].header != 8)) {
      return Fail(error, "Unsupported box size");
    }
    cursor += box_size;
  }
  const size_t xmp_mdat_header = FitsIn(xmp.size() + 8, 4) ? 8 : 16;
  const size_t xmp_offset = cursor + xmp_mdat_header;
  const size_t new_total = xmp_offset + xmp.size();

  // Relocate file-offset extents.
  for (IlocItem& item : iloc.items) {
    if (!item.IsFileOffset()) continue;
    if (&item == xmp_item) {
      item.extents[0].offset = xmp_offset;
      continue;
    }
    for (IlocExtent& extent : item.extents) {
      for (size_t i = 0; i < kept.size(); ++i) {
        const Box& box = kept[i];
        if (extent.offset >= box.offset && extent.offset <= box.end() &&
            extent.length <= box.end() - extent.offset) {
          extent.offset = new_offsets[i] + (extent.offset - box.offset);
          break;
        }
      }
    }
  }
  for (const IlocItem& item : iloc.items) {
    for (const IlocExtent& extent : item.extents) {
      if (!FitsIn(extent.offset, offset_size)) {
        return Fail(error, "'iloc' offset does not fit");
      }
    }
  }
  const std::vector<uint8_t> new_iloc =
      SerializeIloc(iloc, offset_size, length_size);
  if (new_iloc.size() != new_iloc_size) {
    return Fail(error, "Internal 'iloc' size mismatch");
  }

  // 6. Emit.
  output->clear();
  output->reserve(new_total);
  for (size_t i = 0; i < kept.size(); ++i) {
    const Box& box = kept[i];
    if (box.type != kMeta) {
      const size_t start = output->size();
      output->insert(output->end(), data + box.offset, data + box.end());
      if (box.size_was_zero) {
        PatchUint32(output, start, static_cast<uint32_t>(box.size));
      }
      continue;
    }
    if (meta_header == 8) {
      WriteUint(output, new_meta_size, 4);
      WriteUint(output, kMeta, 4);
    } else {
      WriteUint(output, 1, 4);
      WriteUint(output, kMeta, 4);
      WriteUint(output, new_meta_size, 8);
    }
    // version + flags
    output->insert(output->end(), data + box.offset + box.header,
                   data + box.offset + box.header + 4);
    for (const Box& child : meta_children) {
      if (&child == iinf_box) {
        output->insert(output->end(), new_iinf.begin(), new_iinf.end());
      } else if (&child == iloc_box) {
        output->insert(output->end(), new_iloc.begin(), new_iloc.end());
      } else if (&child == iref_box) {
        output->insert(output->end(), new_iref.begin(), new_iref.end());
      } else {
        output->insert(output->end(), data + child.offset, data + child.end());
      }
    }
    if (iref_box == nullptr) {
      output->insert(output->end(), new_iref.begin(), new_iref.end());
    }
  }
  if (xmp_mdat_header == 8) {
    WriteUint(output, xmp.size() + 8, 4);
    WriteUint(output, kMdat, 4);
  } else {
    WriteUint(output, 1, 4);
    WriteUint(output, kMdat, 4);
    WriteUint(output, xmp.size() + 16, 8);
  }
  output->insert(output->end(), xmp.begin(), xmp.end());
  if (output->size() != new_total) {
    return Fail(error, "Internal layout size mismatch");
  }

  // Blank the superseded XMP payload so stale metadata cannot be picked up by
  // tools that scan for XMP packets instead of following 'iloc'.
  if (orphaned_xmp_extent.has_value()) {
    for (size_t i = 0; i < kept.size(); ++i) {
      const Box& box = kept[i];
      const IlocExtent& extent = *orphaned_xmp_extent;
      if (extent.offset >= box.offset && extent.offset <= box.end() &&
          extent.length <= box.end() - extent.offset && box.type != kMeta) {
        std::memset(output->data() + new_offsets[i] +
                        (extent.offset - box.offset),
                    ' ', extent.length);
        break;
      }
    }
  }
  return true;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
