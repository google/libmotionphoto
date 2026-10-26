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

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "image_io/base/data_range.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "motion_photo/container_splitter.h"
#include "motion_photo/tests/test_framework.h"

namespace libmotionphoto {
namespace motion_photo {
namespace {

using Bytes = std::vector<uint8_t>;

constexpr char kPhotoOnlyHeic[] =
    "lib/core/motion_photo/testdata/motion_photo_photo_only.heic";
constexpr char kMotionPhotoHeic[] =
    "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";

Bytes ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
}

// --- Minimal ISO-BMFF writer for synthetic fixtures. ---
void Put(Bytes* out, uint64_t value, size_t num_bytes) {
  for (size_t i = num_bytes; i > 0; --i) {
    out->push_back(static_cast<uint8_t>(value >> (8 * (i - 1))));
  }
}
void PutStr(Bytes* out, std::string_view s) { out->insert(out->end(), s.begin(), s.end()); }
Bytes MakeBox(std::string_view type, const Bytes& payload) {
  Bytes out;
  Put(&out, payload.size() + 8, 4);
  PutStr(&out, type);
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}
Bytes MakeFullBox(std::string_view type, uint8_t version, uint32_t flags,
                  const Bytes& payload) {
  Bytes body;
  Put(&body, version, 1);
  Put(&body, flags, 3);
  body.insert(body.end(), payload.begin(), payload.end());
  return MakeBox(type, body);
}
Bytes Cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}
Bytes Ftyp() {
  Bytes p;
  PutStr(&p, "heic");
  Put(&p, 0, 4);
  PutStr(&p, "mif1heic");
  return MakeBox("ftyp", p);
}
Bytes Hdlr() {
  Bytes p;
  Put(&p, 0, 4);
  PutStr(&p, "pict");
  Put(&p, 0, 12);
  p.push_back(0);
  return MakeFullBox("hdlr", 0, 0, p);
}
Bytes Pitm(uint16_t id) {
  Bytes p;
  Put(&p, id, 2);
  return MakeFullBox("pitm", 0, 0, p);
}
Bytes Infe(uint16_t id, std::string_view type) {
  Bytes p;
  Put(&p, id, 2);
  Put(&p, 0, 2);
  PutStr(&p, type);
  p.push_back(0);
  return MakeFullBox("infe", 2, 0, p);
}
Bytes Iinf(const std::vector<Bytes>& entries) {
  Bytes p;
  Put(&p, entries.size(), 2);
  for (const Bytes& e : entries) p.insert(p.end(), e.begin(), e.end());
  return MakeFullBox("iinf", 0, 0, p);
}
struct ItemLoc {
  uint16_t id;
  uint8_t construction_method;
  uint32_t base_offset;
  uint32_t offset;
  uint32_t length;
};
// iloc version 1, offset/length/base_offset sizes = 4, index_size = 0.
Bytes IlocV1(const std::vector<ItemLoc>& items) {
  Bytes p;
  Put(&p, 0x44, 1);
  Put(&p, 0x40, 1);
  Put(&p, items.size(), 2);
  for (const ItemLoc& item : items) {
    Put(&p, item.id, 2);
    Put(&p, item.construction_method, 2);
    Put(&p, 0, 2);
    Put(&p, item.base_offset, 4);
    Put(&p, 1, 2);
    Put(&p, item.offset, 4);
    Put(&p, item.length, 4);
  }
  return MakeFullBox("iloc", 1, 0, p);
}
Bytes Meta(std::initializer_list<Bytes> children) {
  return MakeFullBox("meta", 0, 0, Cat(children));
}

// --- Minimal reader to resolve item payloads in edited output. ---
struct Parsed {
  uint32_t primary = 0;
  std::map<uint32_t, std::string> item_types;   // id -> item_type
  std::map<uint32_t, std::vector<uint32_t>> cdsc;  // from -> to
  std::map<uint32_t, Bytes> item_data;          // id -> concatenated payload
  std::vector<std::string> top_level;
};

uint64_t Get(const Bytes& d, size_t pos, size_t n) {
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i) v = (v << 8) | d[pos + i];
  return v;
}

std::optional<Parsed> Parse(const Bytes& d) {
  Parsed parsed;
  size_t idat_payload = 0;
  struct Loc {
    uint32_t id;
    int cm;
    std::vector<std::pair<uint64_t, uint64_t>> extents;
  };
  std::vector<Loc> locs;
  size_t pos = 0;
  while (pos + 8 <= d.size()) {
    uint64_t size = Get(d, pos, 4);
    std::string type(d.begin() + pos + 4, d.begin() + pos + 8);
    size_t header = 8;
    if (size == 1) {
      size = Get(d, pos + 8, 8);
      header = 16;
    }
    if (size < header || pos + size > d.size()) return std::nullopt;
    parsed.top_level.push_back(type);
    if (type == "meta") {
      size_t c = pos + header + 4;
      while (c + 8 <= pos + size) {
        uint64_t cs = Get(d, c, 4);
        std::string ct(d.begin() + c + 4, d.begin() + c + 8);
        size_t p = c + 8;
        uint8_t v = d[p];
        if (ct == "pitm") parsed.primary = Get(d, p + 4, v == 0 ? 2 : 4);
        if (ct == "idat") idat_payload = c + 8;
        if (ct == "iinf") {
          size_t e = p + 4 + (v == 0 ? 2 : 4);
          while (e < c + cs) {
            uint64_t es = Get(d, e, 4);
            uint8_t ev = d[e + 8];
            size_t q = e + 12;
            uint32_t id = Get(d, q, ev == 3 ? 4 : 2);
            q += (ev == 3 ? 4 : 2) + 2;
            parsed.item_types[id] = std::string(d.begin() + q, d.begin() + q + 4);
            e += es;
          }
        }
        if (ct == "iref") {
          size_t w = v == 0 ? 2 : 4;
          size_t r = p + 4;
          while (r < c + cs) {
            uint64_t rs = Get(d, r, 4);
            std::string rt(d.begin() + r + 4, d.begin() + r + 8);
            uint32_t from = Get(d, r + 8, w);
            uint64_t n = Get(d, r + 8 + w, 2);
            for (uint64_t i = 0; i < n; ++i) {
              if (rt == "cdsc") parsed.cdsc[from].push_back(Get(d, r + 10 + w + i * w, w));
            }
            r += rs;
          }
        }
        if (ct == "iloc") {
          size_t q = p + 4;
          size_t osz = d[q] >> 4, lsz = d[q] & 15, bsz = d[q + 1] >> 4;
          size_t isz = v >= 1 ? (d[q + 1] & 15) : 0;
          q += 2;
          uint64_t n = Get(d, q, v < 2 ? 2 : 4);
          q += v < 2 ? 2 : 4;
          for (uint64_t i = 0; i < n; ++i) {
            Loc loc;
            loc.id = Get(d, q, v < 2 ? 2 : 4);
            q += v < 2 ? 2 : 4;
            loc.cm = 0;
            if (v >= 1) {
              loc.cm = Get(d, q, 2) & 15;
              q += 2;
            }
            q += 2;  // data_reference_index
            uint64_t base = Get(d, q, bsz);
            q += bsz;
            uint64_t ec = Get(d, q, 2);
            q += 2;
            for (uint64_t e = 0; e < ec; ++e) {
              q += isz;
              uint64_t off = Get(d, q, osz);
              q += osz;
              uint64_t len = Get(d, q, lsz);
              q += lsz;
              loc.extents.push_back({base + off, len});
            }
            locs.push_back(loc);
          }
        }
        c += cs;
      }
    }
    pos += size;
  }
  if (pos != d.size()) return std::nullopt;
  for (const Loc& loc : locs) {
    Bytes data;
    for (auto [off, len] : loc.extents) {
      size_t start = loc.cm == 1 ? idat_payload + off : off;
      if (start + len > d.size()) return std::nullopt;
      data.insert(data.end(), d.begin() + start, d.begin() + start + len);
    }
    parsed.item_data[loc.id] = data;
  }
  return parsed;
}

std::string PrimaryXmpViaSplitter(const Bytes& file) {
  auto segment = image_io::DataSegment::Create(
      image_io::DataRange(0, file.size()), file.data(),
      image_io::DataSegment::BufferDispositionPolicy::kDontDelete);
  image_io::DataSegmentDataSource source(segment);
  image_io::DataRange range = FindHeifPrimaryXmpRange(&source, file.size());
  if (!range.IsValid()) return "";
  return std::string(file.begin() + range.GetBegin(),
                     file.begin() + range.GetEnd());
}

std::string RTrim(std::string s) {
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

std::string MakeXmp(size_t min_size) {
  std::string xmp =
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
      "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
      "<rdf:Description xmlns:GCamera=\"http://ns.google.com/photos/1.0/camera/\" "
      "GCamera:MotionPhoto=\"1\" GCamera:MotionPhotoVersion=\"1\"/>";
  while (xmp.size() + 24 < min_size) xmp += "<!-- padding padding -->";
  xmp += "</rdf:RDF></x:xmpmeta>";
  return xmp;
}

// Asserts every non-XMP item of `before` is byte-identical in `after`.
void ExpectImageItemsUnchanged(const Parsed& before, const Parsed& after,
                               uint32_t xmp_id) {
  for (const auto& [id, data] : before.item_data) {
    if (id == xmp_id) continue;
    auto it = after.item_data.find(id);
    EXPECT_TRUE(it != after.item_data.end());
    if (it != after.item_data.end()) EXPECT_TRUE(it->second == data);
  }
}

}  // namespace

TEST(HeifIsoMetaEditorTest, GrowsPrimaryXmpLosslessly) {
  Bytes input = ReadFile(kPhotoOnlyHeic);
  ASSERT_TRUE(!input.empty());
  std::optional<Parsed> before = Parse(input);
  ASSERT_TRUE(before.has_value());
  const std::string xmp = MakeXmp(4000);  // Larger than the original 1005 B.

  Bytes output;
  std::string error;
  ASSERT_TRUE(HeifIsoMetaEditor::SetPrimaryXmp(input.data(), input.size(), xmp,
                                               &output, &error));
  std::optional<Parsed> after = Parse(output);
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->primary, before->primary);
  EXPECT_TRUE(after->item_data[3] == Bytes(xmp.begin(), xmp.end()));
  EXPECT_EQ(PrimaryXmpViaSplitter(output), xmp);
  ExpectImageItemsUnchanged(*before, *after, 3);
  EXPECT_EQ(after->item_types.size(), before->item_types.size());
  // The superseded XMP bytes are blanked, not left behind.
  std::string out_str(output.begin(), output.end());
  EXPECT_TRUE(out_str.find("xmpmeta") == out_str.rfind("xmpmeta") ||
              out_str.find("xmpmeta") >= out_str.size() - xmp.size());
}

TEST(HeifIsoMetaEditorTest, ShrinksXmpInPlaceAndDropsMpvd) {
  Bytes input = ReadFile(kMotionPhotoHeic);
  ASSERT_TRUE(!input.empty());
  std::optional<Parsed> before = Parse(input);
  ASSERT_TRUE(before.has_value());
  const std::string xmp = MakeXmp(200);

  Bytes output;
  ASSERT_TRUE(HeifIsoMetaEditor::SetPrimaryXmp(input.data(), input.size(), xmp,
                                               &output));
  // The 'mpvd' box starts at 28727: the still image is everything before it.
  EXPECT_EQ(output.size(), 28727u);
  EXPECT_TRUE(Bytes(input.begin(), input.begin() + 434) ==
              Bytes(output.begin(), output.begin() + 434));  // ftyp + meta
  std::optional<Parsed> after = Parse(output);
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->top_level.back(), std::string("mdat"));
  EXPECT_EQ(RTrim(PrimaryXmpViaSplitter(output)), xmp);
  ExpectImageItemsUnchanged(*before, *after, 2);
}

TEST(HeifIsoMetaEditorTest, AddsXmpItemWhenMissing) {
  const std::string image = "HEVC-TILE-PAYLOAD-0123456789";
  Bytes mdat_payload(image.begin(), image.end());
  // Layout: ftyp | meta | mdat. Compute the mdat payload offset first.
  auto build = [&](uint32_t payload_offset) {
    return Cat({Ftyp(),
                Meta({Hdlr(), Pitm(1), Iinf({Infe(1, "hvc1")}),
                      IlocV1({{1, 0, 0, payload_offset,
                               static_cast<uint32_t>(image.size())}})}),
                MakeBox("mdat", mdat_payload)});
  };
  Bytes probe = build(0);
  Bytes input = build(static_cast<uint32_t>(probe.size() - image.size()));
  std::optional<Parsed> before = Parse(input);
  ASSERT_TRUE(before.has_value());
  EXPECT_TRUE(before->item_data[1] == mdat_payload);

  const std::string xmp = MakeXmp(300);
  Bytes output;
  std::string error;
  ASSERT_TRUE(HeifIsoMetaEditor::SetPrimaryXmp(input.data(), input.size(), xmp,
                                               &output, &error));
  std::optional<Parsed> after = Parse(output);
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->item_types[2], std::string("mime"));
  EXPECT_TRUE(after->cdsc[2] == std::vector<uint32_t>{1});
  EXPECT_TRUE(after->item_data[1] == mdat_payload);
  EXPECT_TRUE(after->item_data[2] == Bytes(xmp.begin(), xmp.end()));
  EXPECT_EQ(PrimaryXmpViaSplitter(output), xmp);
}

TEST(HeifIsoMetaEditorTest, HandlesMetaAfterMdatIdatAndTrailingBoxes) {
  // Samsung-style layout: ftyp | mdat | meta(idat, iloc v1 with base offsets)
  // | free | mpvd | sefd.
  const std::string tile = "TILE-BYTES-ABCDEFGHIJ";
  const std::string grid = "GRID";  // Stored in 'idat' (construction_method 1).
  Bytes mdat = MakeBox("mdat", Bytes(tile.begin(), tile.end()));
  const uint32_t tile_offset = static_cast<uint32_t>(Ftyp().size() + 8);
  Bytes input = Cat(
      {Ftyp(), mdat,
       Meta({Hdlr(), Pitm(2), Iinf({Infe(1, "hvc1"), Infe(2, "grid")}),
             IlocV1({{1, 0, tile_offset - 4, 4,
                      static_cast<uint32_t>(tile.size())},
                     {2, 1, 0, 0, static_cast<uint32_t>(grid.size())}}),
             MakeBox("idat", Bytes(grid.begin(), grid.end()))}),
       MakeBox("free", Bytes(16, 0)), MakeBox("mpvd", Bytes(32, 7)),
       MakeBox("sefd", Bytes(8, 9))});
  std::optional<Parsed> before = Parse(input);
  ASSERT_TRUE(before.has_value());
  EXPECT_TRUE(before->item_data[1] == Bytes(tile.begin(), tile.end()));
  EXPECT_TRUE(before->item_data[2] == Bytes(grid.begin(), grid.end()));

  const std::string xmp = MakeXmp(500);
  Bytes output;
  std::string error;
  ASSERT_TRUE(HeifIsoMetaEditor::SetPrimaryXmp(input.data(), input.size(), xmp,
                                               &output, &error));
  std::optional<Parsed> after = Parse(output);
  ASSERT_TRUE(after.has_value());
  EXPECT_TRUE(after->top_level ==
              (std::vector<std::string>{"ftyp", "mdat", "meta", "free", "mdat"}));
  EXPECT_TRUE(after->item_data[1] == Bytes(tile.begin(), tile.end()));
  EXPECT_TRUE(after->item_data[2] == Bytes(grid.begin(), grid.end()));
  EXPECT_TRUE(after->cdsc[3] == std::vector<uint32_t>{2});
  EXPECT_EQ(PrimaryXmpViaSplitter(output), xmp);
  // Data in front of 'meta' does not move.
  EXPECT_TRUE(Bytes(input.begin(), input.begin() + Ftyp().size() + mdat.size()) ==
              Bytes(output.begin(), output.begin() + Ftyp().size() + mdat.size()));
}

TEST(HeifIsoMetaEditorTest, IsIdempotent) {
  Bytes input = ReadFile(kPhotoOnlyHeic);
  const std::string xmp = MakeXmp(3000);
  Bytes once, twice;
  ASSERT_TRUE(HeifIsoMetaEditor::SetPrimaryXmp(input.data(), input.size(), xmp,
                                               &once));
  ASSERT_TRUE(HeifIsoMetaEditor::SetPrimaryXmp(once.data(), once.size(), xmp,
                                               &twice));
  EXPECT_TRUE(once == twice);  // Second pass takes the in-place path.
}

TEST(HeifIsoMetaEditorTest, RejectsMalformedInput) {
  Bytes output;
  std::string error;
  const std::string xmp = MakeXmp(100);
  EXPECT_TRUE(!HeifIsoMetaEditor::SetPrimaryXmp(nullptr, 0, xmp, &output));
  Bytes garbage = {0, 0, 0, 3, 'f', 't', 'y', 'p'};
  EXPECT_TRUE(!HeifIsoMetaEditor::SetPrimaryXmp(garbage.data(), garbage.size(),
                                                xmp, &output, &error));
  Bytes no_meta = Cat({Ftyp(), MakeBox("mdat", Bytes(4, 1))});
  EXPECT_TRUE(!HeifIsoMetaEditor::SetPrimaryXmp(no_meta.data(), no_meta.size(),
                                                xmp, &output, &error));
  Bytes input = ReadFile(kPhotoOnlyHeic);
  for (size_t cut : {size_t{10}, size_t{100}, size_t{300}, input.size() - 1}) {
    EXPECT_TRUE(!HeifIsoMetaEditor::SetPrimaryXmp(input.data(), cut, xmp,
                                                  &output, &error));
  }
  // Item data pointing outside the file is rejected rather than relocated.
  Bytes bad = Cat({Ftyp(),
                   Meta({Hdlr(), Pitm(1), Iinf({Infe(1, "hvc1")}),
                         IlocV1({{1, 0, 0, 100000, 10}})}),
                   MakeBox("mdat", Bytes(4, 1))});
  EXPECT_TRUE(!HeifIsoMetaEditor::SetPrimaryXmp(bad.data(), bad.size(),
                                                MakeXmp(5000), &output, &error));
}

}  // namespace motion_photo
}  // namespace libmotionphoto
