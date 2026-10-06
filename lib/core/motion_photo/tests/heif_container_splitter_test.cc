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

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "image_io/base/data_range.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "metadata_collection.pb.h"
#include "motion_photo/container_splitter.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/tests/test_framework.h"

namespace libmotionphoto {
namespace motion_photo {
namespace {

using Bytes = std::vector<uint8_t>;

constexpr char kTestDataDir[] = "lib/core/motion_photo/testdata/";
constexpr char kHeicMotionPhoto[] = "motion_photo_single_video_track.MP.heic";
// Motion photo XMP without the 'mpvd' box, i.e. the video was removed.
constexpr char kHeicWithoutVideo[] = "motion_photo_photo_only.heic";
constexpr std::string_view kXmpContentType = "application/rdf+xml";

Bytes ReadTestFile(const std::string& name) {
  std::ifstream file(kTestDataDir + name, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(file),
               std::istreambuf_iterator<char>());
}

std::string AsString(const Bytes& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

// Splits the first `size` bytes of `data` with the HEIF splitter.
std::vector<RawMetadataBlock> SplitHeif(const uint8_t* data, size_t size) {
  std::shared_ptr<image_io::DataSegment> segment =
      image_io::DataSegment::Create(image_io::DataRange(0, size), data,
                                    image_io::DataSegment::kDontDelete);
  image_io::DataSegmentDataSource data_source(segment);
  return CreateHeifContainerSplitter()->Split(&data_source, size);
}

std::vector<RawMetadataBlock> SplitHeif(const Bytes& file) {
  return SplitHeif(file.data(), file.size());
}

// Runs the full MetadataEngine pipeline and returns its motion photo verdict.
bool IsMotionPhotoPerEngine(const Bytes& file, FileType file_type) {
  std::shared_ptr<image_io::DataSegment> segment =
      image_io::DataSegment::Create(image_io::DataRange(0, file.size()),
                                    file.data(),
                                    image_io::DataSegment::kDontDelete);
  image_io::DataSegmentDataSource data_source(segment);
  MetadataEngine engine(/*message_handler=*/nullptr);
  return MetadataEngine::IsMotionPhoto(
      engine.Parse(&data_source, file.size(), file_type));
}

// Helpers that write ISO BMFF boxes for synthetic HEIF files.

void AppendUInt(uint64_t value, size_t num_bytes, Bytes* out) {
  for (size_t i = num_bytes; i > 0; --i) {
    out->push_back(static_cast<uint8_t>(value >> (8 * (i - 1))));
  }
}

void AppendString(std::string_view text, Bytes* out) {
  out->insert(out->end(), text.begin(), text.end());
}

void AppendCString(std::string_view text, Bytes* out) {
  AppendString(text, out);
  out->push_back('\0');
}

Bytes Concat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

Bytes MakeBox(std::string_view type, const Bytes& payload) {
  Bytes box;
  AppendUInt(8 + payload.size(), 4, &box);
  AppendString(type, &box);
  return Concat({box, payload});
}

Bytes MakeFullBox(std::string_view type, uint8_t version,
                  const Bytes& payload) {
  Bytes header;
  AppendUInt(version, 1, &header);
  AppendUInt(0, 3, &header);  // flags
  return MakeBox(type, Concat({header, payload}));
}

Bytes MakeFtypBox() {
  Bytes payload;
  AppendString("heic", &payload);
  AppendUInt(0, 4, &payload);  // minor_version
  AppendString("mif1heic", &payload);
  return MakeBox("ftyp", payload);
}

Bytes MakeMpvdBox() {
  Bytes video;
  AppendString("not really a video", &video);
  return MakeBox("mpvd", video);
}

Bytes MakePitmV0(uint16_t item_id) {
  Bytes payload;
  AppendUInt(item_id, 2, &payload);
  return MakeFullBox("pitm", 0, payload);
}

// An 'infe' version 2 box. `content_type` is only written for 'mime' items.
Bytes MakeInfeV2(uint16_t item_id, std::string_view item_type,
                 std::string_view content_type) {
  Bytes payload;
  AppendUInt(item_id, 2, &payload);
  AppendUInt(0, 2, &payload);  // item_protection_index
  AppendString(item_type, &payload);
  AppendCString("", &payload);  // item_name
  if (item_type == "mime") {
    AppendCString(content_type, &payload);
  }
  return MakeFullBox("infe", 2, payload);
}

Bytes MakeIinfV0(std::initializer_list<Bytes> entries) {
  Bytes payload;
  AppendUInt(entries.size(), 2, &payload);
  return MakeFullBox("iinf", 0, Concat({payload, Concat(entries)}));
}

// An 'iref' version 0 box with one 'cdsc' reference per {from, to} item pair.
Bytes MakeCdscIrefV0(
    std::initializer_list<std::pair<uint16_t, uint16_t>> references) {
  Bytes payload;
  for (const auto& [from_item_id, to_item_id] : references) {
    Bytes cdsc;
    AppendUInt(from_item_id, 2, &cdsc);
    AppendUInt(1, 2, &cdsc);  // reference_count
    AppendUInt(to_item_id, 2, &cdsc);
    payload = Concat({payload, MakeBox("cdsc", cdsc)});
  }
  return MakeFullBox("iref", 0, payload);
}

struct IdatItem {
  uint16_t item_id = 0;
  uint32_t offset = 0;
  uint32_t length = 0;
  uint16_t data_reference_index = 0;
};

// An 'iloc' version 1 box in which every item is one extent of the 'idat' box
// (construction method 1), with 4-byte offsets and lengths.
Bytes MakeIdatIlocV1(std::initializer_list<IdatItem> items) {
  Bytes payload;
  AppendUInt(0x4400, 2, &payload);  // offset_size 4, length_size 4, others 0.
  AppendUInt(items.size(), 2, &payload);
  for (const IdatItem& item : items) {
    AppendUInt(item.item_id, 2, &payload);
    AppendUInt(1, 2, &payload);  // construction_method
    AppendUInt(item.data_reference_index, 2, &payload);
    AppendUInt(1, 2, &payload);  // extent_count
    AppendUInt(item.offset, 4, &payload);
    AppendUInt(item.length, 4, &payload);
  }
  return MakeFullBox("iloc", 1, payload);
}

// Builds a HEIF file whose primary item 1 is described by item 2, a 'mime' item
// stored in 'idat' after `idat_prefix`.
Bytes MakeHeifWithIdatItem(std::string_view content_type,
                           std::string_view idat_prefix,
                           std::string_view item_data,
                           uint16_t data_reference_index) {
  Bytes idat;
  AppendString(idat_prefix, &idat);
  AppendString(item_data, &idat);
  const IdatItem item = {2, static_cast<uint32_t>(idat_prefix.size()),
                         static_cast<uint32_t>(item_data.size()),
                         data_reference_index};
  return Concat(
      {MakeFtypBox(),
       MakeFullBox("meta", 0,
                   Concat({MakePitmV0(1),
                           MakeIinfV0({MakeInfeV2(1, "hvc1", ""),
                                       MakeInfeV2(2, "mime", content_type)}),
                           MakeCdscIrefV0({{2, 1}}), MakeIdatIlocV1({item}),
                           MakeBox("idat", idat)})),
       MakeMpvdBox()});
}

TEST(HeifContainerSplitterTest, ExtractsXmpFromHeicMotionPhoto) {
  const Bytes file = ReadTestFile(kHeicMotionPhoto);
  ASSERT_FALSE(file.empty());

  std::vector<RawMetadataBlock> blocks = SplitHeif(file);

  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].type, "XMP");
  EXPECT_EQ(blocks[0].format_identifier, "standard.xmp");
  // The XMP of this file is item 2, a single 1111-byte extent at 27616.
  EXPECT_EQ(blocks[0].offset, 27616u);
  EXPECT_TRUE(blocks[0].bytes ==
              Bytes(file.begin() + 27616, file.begin() + 27616 + 1111));
}

TEST(HeifContainerSplitterTest, IgnoresHeicWithoutMpvdBox) {
  const Bytes file = ReadTestFile(kHeicWithoutVideo);
  ASSERT_FALSE(file.empty());

  EXPECT_TRUE(SplitHeif(file).empty());
}

TEST(HeifContainerSplitterTest, IgnoresJpeg) {
  const Bytes file = ReadTestFile("motion_photo_single_video_track.MP.jpg");
  ASSERT_FALSE(file.empty());

  EXPECT_TRUE(SplitHeif(file).empty());
}

TEST(HeifContainerSplitterTest, IgnoresNullDataSource) {
  EXPECT_TRUE(CreateHeifContainerSplitter()->Split(nullptr, 1024).empty());
}

TEST(HeifContainerSplitterTest, ExtractsXmpStoredInIdat) {
  const std::string_view prefix = "pad";
  const std::string_view xmp = "<x:xmpmeta>stored in idat</x:xmpmeta>";
  const Bytes file = MakeHeifWithIdatItem(kXmpContentType, prefix, xmp,
                                          /*data_reference_index=*/0);

  std::vector<RawMetadataBlock> blocks = SplitHeif(file);

  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(AsString(blocks[0].bytes), std::string(xmp));
  // 'idat' is the last box before 'mpvd', so its payload ends there.
  const size_t idat_payload_offset =
      file.size() - MakeMpvdBox().size() - prefix.size() - xmp.size();
  EXPECT_EQ(blocks[0].offset, idat_payload_offset + prefix.size());
}

TEST(HeifContainerSplitterTest, IgnoresNonXmpMimeItem) {
  const Bytes file =
      MakeHeifWithIdatItem("application/octet-stream", "", "<x:xmpmeta/>",
                           /*data_reference_index=*/0);

  EXPECT_TRUE(SplitHeif(file).empty());
}

TEST(HeifContainerSplitterTest, IgnoresXmpStoredInAnotherFile) {
  const Bytes file = MakeHeifWithIdatItem(kXmpContentType, "", "<x:xmpmeta/>",
                                          /*data_reference_index=*/1);

  EXPECT_TRUE(SplitHeif(file).empty());
}

TEST(HeifContainerSplitterTest, PrefersXmpThatDescribesPrimaryItem) {
  const std::string_view other_xmp = "<x:xmpmeta>other</x:xmpmeta>";
  const std::string_view primary_xmp = "<x:xmpmeta>primary</x:xmpmeta>";
  Bytes idat;
  AppendString(other_xmp, &idat);
  AppendString(primary_xmp, &idat);
  const IdatItem other_item = {2, 0, static_cast<uint32_t>(other_xmp.size())};
  const IdatItem primary_item = {3, static_cast<uint32_t>(other_xmp.size()),
                                 static_cast<uint32_t>(primary_xmp.size())};
  // XMP item 2 comes first but describes item 4; XMP item 3 describes the
  // primary item 1.
  const Bytes file = Concat(
      {MakeFtypBox(),
       MakeFullBox("meta", 0,
                   Concat({MakePitmV0(1),
                           MakeIinfV0({MakeInfeV2(1, "hvc1", ""),
                                       MakeInfeV2(2, "mime", kXmpContentType),
                                       MakeInfeV2(3, "mime", kXmpContentType)}),
                           MakeCdscIrefV0({{2, 4}, {3, 1}}),
                           MakeIdatIlocV1({other_item, primary_item}),
                           MakeBox("idat", idat)})),
       MakeMpvdBox()});

  std::vector<RawMetadataBlock> blocks = SplitHeif(file);

  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(AsString(blocks[0].bytes), std::string(primary_xmp));
}

// Covers 32-bit item IDs ('pitm' v1, 'infe' v3, 'iref' v1), 'iloc' v2 with
// 8-byte fields, base offsets and extent indices, and multi-extent items.
TEST(HeifContainerSplitterTest, ReadsMultiExtentItemWithWideFields) {
  constexpr uint32_t kPrimaryItemId = 0x10001;
  constexpr uint32_t kXmpItemId = 0x10002;
  const std::string_view first_part = "<x:xmpmeta>first|";
  const std::string_view gap = "not part of the item";
  const std::string_view second_part = "second</x:xmpmeta>";
  Bytes mdat;
  AppendString(first_part, &mdat);
  AppendString(gap, &mdat);
  AppendString(second_part, &mdat);

  Bytes pitm;
  AppendUInt(kPrimaryItemId, 4, &pitm);
  Bytes image_infe;
  AppendUInt(kPrimaryItemId, 4, &image_infe);
  AppendUInt(0, 2, &image_infe);  // item_protection_index
  AppendString("hvc1", &image_infe);
  AppendCString("", &image_infe);
  Bytes xmp_infe;
  AppendUInt(kXmpItemId, 4, &xmp_infe);
  AppendUInt(0, 2, &xmp_infe);  // item_protection_index
  AppendString("mime", &xmp_infe);
  AppendCString("XMP", &xmp_infe);
  AppendCString(kXmpContentType, &xmp_infe);
  Bytes iinf;
  AppendUInt(2, 4, &iinf);  // entry_count
  iinf = Concat({iinf, MakeFullBox("infe", 3, image_infe),
                 MakeFullBox("infe", 3, xmp_infe)});
  Bytes cdsc;
  AppendUInt(kXmpItemId, 4, &cdsc);
  AppendUInt(1, 2, &cdsc);  // reference_count
  AppendUInt(kPrimaryItemId, 4, &cdsc);
  // The size of 'meta' does not depend on the base offset it records.
  auto make_meta = [&](uint32_t base_offset) {
    Bytes iloc;
    AppendUInt(0x8844, 2, &iloc);  // offset 8, length 8, base 4, index 4.
    AppendUInt(1, 4, &iloc);       // item_count
    AppendUInt(kXmpItemId, 4, &iloc);
    AppendUInt(0, 2, &iloc);  // construction_method: file offsets.
    AppendUInt(0, 2, &iloc);  // data_reference_index
    AppendUInt(base_offset, 4, &iloc);
    AppendUInt(2, 2, &iloc);  // extent_count
    AppendUInt(0, 4, &iloc);  // extent_index
    AppendUInt(0, 8, &iloc);
    AppendUInt(first_part.size(), 8, &iloc);
    AppendUInt(0, 4, &iloc);  // extent_index
    AppendUInt(first_part.size() + gap.size(), 8, &iloc);
    AppendUInt(second_part.size(), 8, &iloc);
    return MakeFullBox(
        "meta", 0,
        Concat({MakeFullBox("pitm", 1, pitm), MakeFullBox("iinf", 1, iinf),
                MakeFullBox("iref", 1, MakeBox("cdsc", cdsc)),
                MakeFullBox("iloc", 2, iloc)}));
  };
  const Bytes ftyp = MakeFtypBox();
  const uint32_t mdat_payload_offset =
      static_cast<uint32_t>(ftyp.size() + make_meta(0).size() + 8);
  const Bytes file = Concat({ftyp, make_meta(mdat_payload_offset),
                             MakeBox("mdat", mdat), MakeMpvdBox()});

  std::vector<RawMetadataBlock> blocks = SplitHeif(file);

  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(AsString(blocks[0].bytes),
            std::string(first_part) + std::string(second_part));
  EXPECT_EQ(blocks[0].offset, static_cast<size_t>(mdat_payload_offset));
}

TEST(HeifContainerSplitterTest, SurvivesTruncatedInput) {
  const Bytes file = ReadTestFile(kHeicMotionPhoto);
  ASSERT_FALSE(file.empty());

  for (size_t size = 1; size < file.size(); size += 7) {
    std::vector<RawMetadataBlock> blocks = SplitHeif(file.data(), size);
    EXPECT_TRUE(blocks.size() <= 1u);
    for (const RawMetadataBlock& block : blocks) {
      EXPECT_TRUE(block.offset + block.bytes.size() <= size);
    }
  }
}

TEST(HeifContainerSplitterTest, SurvivesCorruptedBoxes) {
  Bytes file = ReadTestFile(kHeicMotionPhoto);
  ASSERT_FALSE(file.empty());

  // Corrupts each byte of the 'ftyp' and 'meta' boxes, which end at 434.
  for (size_t i = 0; i < 434; ++i) {
    const uint8_t original = file[i];
    for (uint8_t value : {uint8_t{0x00}, uint8_t{0xFF},
                          static_cast<uint8_t>(original ^ 0x80)}) {
      file[i] = value;
      std::vector<RawMetadataBlock> blocks = SplitHeif(file);
      EXPECT_TRUE(blocks.size() <= 1u);
      for (const RawMetadataBlock& block : blocks) {
        EXPECT_TRUE(block.offset + block.bytes.size() <= file.size());
      }
    }
    file[i] = original;
  }
}

TEST(MetadataEngineHeifTest, DetectsHeifMotionPhoto) {
  const Bytes file = ReadTestFile(kHeicMotionPhoto);
  ASSERT_FALSE(file.empty());

  EXPECT_TRUE(IsMotionPhotoPerEngine(file, FileType::kHeic));
  EXPECT_TRUE(IsMotionPhotoPerEngine(file, FileType::kAvif));
}

TEST(MetadataEngineHeifTest, RejectsHeicWithoutVideo) {
  const Bytes file = ReadTestFile(kHeicWithoutVideo);
  ASSERT_FALSE(file.empty());

  EXPECT_FALSE(IsMotionPhotoPerEngine(file, FileType::kHeic));
}

}  // namespace
}  // namespace motion_photo
}  // namespace libmotionphoto
