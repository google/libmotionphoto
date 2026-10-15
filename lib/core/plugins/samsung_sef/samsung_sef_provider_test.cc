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

#include "plugins/samsung_sef/samsung_sef_provider.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "libmotionphoto_api.h"
#include "motion_photo/container_splitter.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/tests/test_framework.h"
#include "samsung_sef_metadata.pb.h"  // NOLINT

namespace libmotionphoto {
namespace motion_photo {
namespace {

void AppendLe16(std::vector<uint8_t>* buf, uint16_t v) {
  buf->push_back(static_cast<uint8_t>(v & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void AppendLe32(std::vector<uint8_t>* buf, uint32_t v) {
  buf->push_back(static_cast<uint8_t>(v & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void AppendBe32(std::vector<uint8_t>* buf, uint32_t v) {
  buf->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf->push_back(static_cast<uint8_t>(v & 0xFF));
}

void AppendStr(std::vector<uint8_t>* buf, const std::string& s) {
  buf->insert(buf->end(), s.begin(), s.end());
}

std::vector<uint8_t> BuildFakeMp4Payload() {
  std::vector<uint8_t> mp4;
  // 24-byte ftyp box + 16-byte mdat box = 40 bytes
  AppendBe32(&mp4, 24);
  AppendStr(&mp4, "ftypmp42");
  AppendBe32(&mp4, 0);
  AppendStr(&mp4, "mp42isom");
  AppendBe32(&mp4, 16);
  AppendStr(&mp4, "mdat");
  AppendStr(&mp4, "VIDEODAT");
  return mp4;
}

struct SefRecordSpec {
  uint16_t type;
  std::string key;
  std::vector<uint8_t> payload;
};

// Builds a complete SEF binary trailer (records + SEFH directory + SEFT tail).
std::vector<uint8_t> BuildSefTrailer(uint32_t sef_version,
                                     const std::vector<SefRecordSpec>& recs) {
  std::vector<uint8_t> out;
  struct EntryInfo {
    uint16_t type;
    size_t start_offset;
    size_t length;
  };
  std::vector<EntryInfo> entries;

  for (const auto& r : recs) {
    size_t start = out.size();
    AppendLe16(&out, 0);
    AppendLe16(&out, r.type);
    AppendLe32(&out, static_cast<uint32_t>(r.key.size()));
    AppendStr(&out, r.key);
    out.insert(out.end(), r.payload.begin(), r.payload.end());
    size_t len = out.size() - start;
    entries.push_back({r.type, start, len});
  }

  size_t sefh_pos = out.size();
  AppendStr(&out, "SEFH");
  AppendLe32(&out, sef_version);
  AppendLe32(&out, static_cast<uint32_t>(entries.size()));
  for (const auto& e : entries) {
    AppendLe16(&out, 0);
    AppendLe16(&out, e.type);
    AppendLe32(&out, static_cast<uint32_t>(sefh_pos - e.start_offset));
    AppendLe32(&out, static_cast<uint32_t>(e.length));
  }

  uint32_t sefh_back_offset = static_cast<uint32_t>(out.size() - sefh_pos);
  AppendLe32(&out, sefh_back_offset);
  AppendStr(&out, "SEFT");
  return out;
}

TEST(SamsungSefProviderTest, IdentifiesValidSefTrailerAndSefdBox) {
  SamsungSefProvider provider;
  EXPECT_EQ(provider.GetPluginName(), "samsung_sef");

  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();
  std::vector<uint8_t> mpv3_payload;
  AppendStr(&mpv3_payload, "mpv3");
  AppendBe32(&mpv3_payload, 100);
  AppendBe32(&mpv3_payload, static_cast<uint32_t>(mp4.size()));
  mpv3_payload.insert(mpv3_payload.end(), mp4.begin(), mp4.end());

  std::vector<uint8_t> trailer =
      BuildSefTrailer(107, {{0x0A30, "MotionPhoto_Data", mpv3_payload}});

  RawMetadataBlock jpeg_block;
  jpeg_block.type = "TRAILER";
  jpeg_block.format_identifier = "container.trailer";
  jpeg_block.bytes = trailer;
  EXPECT_TRUE(provider.Identify(jpeg_block));

  RawMetadataBlock heic_block = jpeg_block;
  heic_block.type = "SEFD";
  heic_block.format_identifier = "heic.sefd";
  EXPECT_TRUE(provider.Identify(heic_block));

  RawMetadataBlock wrong_format = jpeg_block;
  wrong_format.format_identifier = "standard.xmp";
  EXPECT_FALSE(provider.Identify(wrong_format));

  RawMetadataBlock too_short = jpeg_block;
  too_short.bytes = {'S', 'E', 'F', 'T'};
  EXPECT_FALSE(provider.Identify(too_short));
}

TEST(SamsungSefProviderTest, DecodesSef107Mpv3AndExtractsVideoInfo) {
  SamsungSefProvider provider;

  std::vector<uint8_t> utc_payload = {'1', '7', '5', '9', '2', '4', '0', '0'};
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();
  std::vector<uint8_t> mpv3_payload;
  AppendStr(&mpv3_payload, "mpv3");
  AppendBe32(&mpv3_payload, 500);
  AppendBe32(&mpv3_payload, static_cast<uint32_t>(mp4.size()));
  mpv3_payload.insert(mpv3_payload.end(), mp4.begin(), mp4.end());

  std::vector<uint8_t> trailer =
      BuildSefTrailer(107, {{0x0C61, "Image_UTC_Data", utc_payload},
                            {0x0A30, "MotionPhoto_Data", mpv3_payload}});

  RawMetadataBlock block;
  block.type = "TRAILER";
  block.format_identifier = "container.trailer";
  block.bytes = trailer;

  std::vector<uint8_t> proto_bytes;
  std::string type_url;
  std::string error_msg;
  ASSERT_TRUE(
      provider.DecodeToSemantic(block, &proto_bytes, &type_url, &error_msg));
  EXPECT_EQ(
      type_url,
      "type.googleapis.com/libmotionphoto.motion_photo.SamsungSefMetadata");

  std::string payload_str(proto_bytes.begin(), proto_bytes.end());
  EXPECT_TRUE(provider.IsMotionPhoto(type_url, payload_str));

  SamsungSefMetadata meta;
  ASSERT_TRUE(meta.ParseFromString(payload_str));
  EXPECT_EQ(meta.sef_version(), 107u);
  EXPECT_EQ(meta.sef_entry_count(), 2u);
  EXPECT_TRUE(meta.has_motion_photo_data());
  EXPECT_EQ(meta.motion_photo_data_magic(), "mpv3");
  EXPECT_EQ(meta.video_length(), mp4.size());
  ASSERT_EQ(meta.sef_keys_size(), 2);
  EXPECT_EQ(meta.sef_keys(0), "Image_UTC_Data");
  EXPECT_EQ(meta.sef_keys(1), "MotionPhoto_Data");

  size_t video_offset_in_block = 0;
  size_t video_length = 0;
  ASSERT_TRUE(
      provider.GetVideoInfo(block, &video_offset_in_block, &video_length));
  EXPECT_EQ(video_length, mp4.size());
  EXPECT_TRUE(video_offset_in_block + video_length <= block.bytes.size());
  EXPECT_EQ(std::memcmp(block.bytes.data() + video_offset_in_block + 4, "ftyp",
                        4),
            0);
}

TEST(SamsungSefProviderTest, DecodesHeicSefdPointerToMpvd) {
  SamsungSefProvider provider;

  std::vector<uint8_t> mpv3_ptr;
  AppendStr(&mpv3_ptr, "mpv3");
  AppendBe32(&mpv3_ptr, 0x00168831);  // mpvd payload offset in file
  AppendBe32(&mpv3_ptr, 0x003CBD5E);  // mpvd payload size in file

  std::vector<uint8_t> sefd_bytes =
      BuildSefTrailer(107, {{0x0A30, "MotionPhoto_Data", mpv3_ptr}});

  RawMetadataBlock sefd_block;
  sefd_block.type = "SEFD";
  sefd_block.format_identifier = "heic.sefd";
  sefd_block.offset = 0x00168831;
  sefd_block.total_payload_size = 0x003CBD5E;
  sefd_block.bytes = sefd_bytes;

  size_t offset_in_mpvd = 999;
  size_t video_len = 0;
  ASSERT_TRUE(provider.GetVideoInfo(sefd_block, &offset_in_mpvd, &video_len));
  EXPECT_EQ(offset_in_mpvd, 0u);
  EXPECT_EQ(video_len, 0x003CBD5Eu);

  // Samsung Smart Switch HEIC (SEF v106) where 0x0A30 MotionPhoto_Data has a
  // 0-byte stub payload and the video lives in mpvd.
  RawMetadataBlock smart_switch_sefd = sefd_block;
  smart_switch_sefd.bytes =
      BuildSefTrailer(106, {{0x0A30, "MotionPhoto_Data", {}}});
  offset_in_mpvd = 999;
  video_len = 0;
  ASSERT_TRUE(
      provider.GetVideoInfo(smart_switch_sefd, &offset_in_mpvd, &video_len));
  EXPECT_EQ(offset_in_mpvd, 0u);
  EXPECT_EQ(video_len, 0x003CBD5Eu);
}

TEST(SamsungSefProviderTest, DecodesSef106Mpv2AndRawMp4) {
  SamsungSefProvider provider;
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();

  // Case A: SEF v106 with "mpv2" 12-byte header.
  std::vector<uint8_t> mpv2_payload;
  AppendStr(&mpv2_payload, "mpv2");
  AppendBe32(&mpv2_payload, 200);
  AppendBe32(&mpv2_payload, static_cast<uint32_t>(mp4.size()));
  mpv2_payload.insert(mpv2_payload.end(), mp4.begin(), mp4.end());

  RawMetadataBlock mpv2_block;
  mpv2_block.format_identifier = "container.trailer";
  mpv2_block.bytes =
      BuildSefTrailer(106, {{0x0A30, "MotionPhoto_Data", mpv2_payload}});

  std::vector<uint8_t> proto_bytes;
  std::string type_url;
  ASSERT_TRUE(
      provider.DecodeToSemantic(mpv2_block, &proto_bytes, &type_url, nullptr));
  SamsungSefMetadata meta_mpv2;
  ASSERT_TRUE(meta_mpv2.ParseFromArray(proto_bytes.data(), proto_bytes.size()));
  EXPECT_EQ(meta_mpv2.sef_version(), 106u);
  EXPECT_EQ(meta_mpv2.motion_photo_data_magic(), "mpv2");
  EXPECT_EQ(meta_mpv2.video_length(), mp4.size());

  // Case B: Legacy SEF v106 where MotionPhoto_Data contains raw MP4 directly.
  RawMetadataBlock raw_mp4_block;
  raw_mp4_block.format_identifier = "container.trailer";
  raw_mp4_block.bytes =
      BuildSefTrailer(106, {{0x0A30, "MotionPhoto_Data", mp4}});

  proto_bytes.clear();
  ASSERT_TRUE(provider.DecodeToSemantic(raw_mp4_block, &proto_bytes, &type_url,
                                        nullptr));
  SamsungSefMetadata meta_raw;
  ASSERT_TRUE(meta_raw.ParseFromArray(proto_bytes.data(), proto_bytes.size()));
  EXPECT_EQ(meta_raw.sef_version(), 106u);
  EXPECT_TRUE(meta_raw.motion_photo_data_magic().empty());
  EXPECT_EQ(meta_raw.video_length(), mp4.size());
}

TEST(SamsungSefProviderTest, HeifSplitterExtractsSefdAndAssociatesMpvd) {
  // Construct a synthetic Samsung HEIC file containing:
  // [ftyp box][mpvd box][sefd box]
  std::vector<uint8_t> heic;
  // ftyp box (16 bytes)
  AppendBe32(&heic, 16);
  AppendStr(&heic, "ftypheic");
  AppendBe32(&heic, 0);

  // mpvd box (8-byte header + 40-byte fake MP4)
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();
  size_t mpvd_payload_offset = heic.size() + 8;
  AppendBe32(&heic, static_cast<uint32_t>(8 + mp4.size()));
  AppendStr(&heic, "mpvd");
  heic.insert(heic.end(), mp4.begin(), mp4.end());

  // sefd box (8-byte header + SEF v107 directory pointing to mpvd)
  std::vector<uint8_t> mpv3_ptr;
  AppendStr(&mpv3_ptr, "mpv3");
  AppendBe32(&mpv3_ptr, static_cast<uint32_t>(mpvd_payload_offset));
  AppendBe32(&mpv3_ptr, static_cast<uint32_t>(mp4.size()));
  std::vector<uint8_t> sefd_payload =
      BuildSefTrailer(107, {{0x0A30, "MotionPhoto_Data", mpv3_ptr}});
  AppendBe32(&heic, static_cast<uint32_t>(8 + sefd_payload.size()));
  AppendStr(&heic, "sefd");
  heic.insert(heic.end(), sefd_payload.begin(), sefd_payload.end());

  auto segment = image_io::DataSegment::Create(
      image_io::DataRange(0, heic.size()), heic.data(),
      image_io::DataSegment::BufferDispositionPolicy::kDontDelete);
  image_io::DataSegmentDataSource data_source(segment);

  auto splitter = CreateHeifContainerSplitter();
  std::vector<RawMetadataBlock> blocks =
      splitter->Split(&data_source, heic.size());
  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].format_identifier, "heic.sefd");
  EXPECT_EQ(blocks[0].offset, mpvd_payload_offset);
  EXPECT_EQ(blocks[0].total_payload_size, mp4.size());

  MetadataEngine engine(nullptr);
  MetadataCollection collection =
      engine.Parse(&data_source, heic.size(), FileType::kHeic);
  EXPECT_TRUE(MetadataEngine::IsMotionPhoto(collection));
  ASSERT_EQ(collection.blocks_size(), 1);
  EXPECT_EQ(collection.blocks(0).offset(), mpvd_payload_offset);
  EXPECT_EQ(collection.blocks(0).video_offset_in_block(), 0u);
  EXPECT_EQ(collection.blocks(0).video_length(), mp4.size());
}

TEST(SamsungSefProviderTest, EndToEndPureSefJpegSplitAndExtract) {
  // Construct a minimal JPEG (SOI + EOI = 4 bytes) followed by a multi-record
  // Samsung SEF trailer (Image_UTC_Data before MotionPhoto_Data).
  std::vector<uint8_t> jpeg_bytes = {0xFF, 0xD8, 0xFF, 0xD9};
  std::vector<uint8_t> utc_payload = {'2', '0', '2', '6', '1', '0', '0', '1'};
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();
  std::vector<uint8_t> mpv3_payload;
  AppendStr(&mpv3_payload, "mpv3");
  AppendBe32(&mpv3_payload, 52);
  AppendBe32(&mpv3_payload, static_cast<uint32_t>(mp4.size()));
  mpv3_payload.insert(mpv3_payload.end(), mp4.begin(), mp4.end());

  std::vector<uint8_t> trailer =
      BuildSefTrailer(107, {{0x0C61, "Image_UTC_Data", utc_payload},
                            {0x0A30, "MotionPhoto_Data", mpv3_payload}});

  std::vector<uint8_t> full_file = jpeg_bytes;
  full_file.insert(full_file.end(), trailer.begin(), trailer.end());

  // 1. Default HandlerOptions (3P plugins enabled): detected & extracted.
  libmotionphoto::api::MotionPhotoMetadata meta;
  libmotionphoto::api::HandlerOptions opts;
  EXPECT_TRUE(libmotionphoto::api::ParseMotionPhotoFromMemory(
      full_file.data(), full_file.size(), &meta, nullptr, opts));
  EXPECT_TRUE(meta.is_motion_photo);
  EXPECT_EQ(meta.video_length, mp4.size());

  // 2. When 3P plugins are disabled, pure SEF (without GCamera XMP) is rejected.
  libmotionphoto::api::HandlerOptions disabled_opts;
  disabled_opts.disable_3p_plugins = true;
  libmotionphoto::api::MotionPhotoMetadata disabled_meta;
  EXPECT_FALSE(libmotionphoto::api::ParseMotionPhotoFromMemory(
      full_file.data(), full_file.size(), &disabled_meta, nullptr,
      disabled_opts));

  // 3. ExtractMotionPhotoFromFile strips the entire SEF trailer (including
  // Image_UTC_Data that precedes MotionPhoto_Data) and extracts the exact MP4
  // stream (without the 12-byte mpv3 prefix or trailing SEFH/SEFT directory).
  const std::string in_path = "/tmp/samsung_sef_test_input.jpg";
  const std::string out_img_path = "/tmp/samsung_sef_test_primary.jpg";
  const std::string out_vid_path = "/tmp/samsung_sef_test_video.mp4";
  {
    std::ofstream out(in_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(full_file.data()),
              full_file.size());
  }

  libmotionphoto::api::ExtractorParams extract_params;
  extract_params.motion_photo_filepath = in_path;
  extract_params.primary_image_output_path = out_img_path;
  extract_params.video_output_path = out_vid_path;
  ASSERT_TRUE(libmotionphoto::api::ExtractMotionPhotoFromFile(extract_params));

  std::ifstream img_in(out_img_path, std::ios::binary);
  std::vector<uint8_t> extracted_img((std::istreambuf_iterator<char>(img_in)),
                                     std::istreambuf_iterator<char>());
  EXPECT_TRUE(extracted_img == jpeg_bytes);

  std::ifstream vid_in(out_vid_path, std::ios::binary);
  std::vector<uint8_t> extracted_video((std::istreambuf_iterator<char>(vid_in)),
                                       std::istreambuf_iterator<char>());
  EXPECT_TRUE(extracted_video == mp4);

  std::remove(in_path.c_str());
  std::remove(out_img_path.c_str());
  std::remove(out_vid_path.c_str());
}

TEST(SamsungSefProviderTest, RejectsMalformedAndCorruptedSefBlocksSafely) {
  SamsungSefProvider provider;

  // Invalid SEFH backward offset larger than block size.
  RawMetadataBlock bad_back_offset;
  bad_back_offset.format_identifier = "container.trailer";
  bad_back_offset.bytes.resize(32, 0);
  bad_back_offset.bytes[24] = 0xFF;
  bad_back_offset.bytes[25] = 0xFF;
  std::memcpy(bad_back_offset.bytes.data() + 28, "SEFT", 4);
  EXPECT_TRUE(provider.Identify(bad_back_offset));

  std::vector<uint8_t> proto;
  std::string url;
  std::string err;
  EXPECT_FALSE(provider.DecodeToSemantic(bad_back_offset, &proto, &url, &err));

  // Valid SEFH header but entry_count is huge (overflow test).
  std::vector<uint8_t> huge_count;
  AppendStr(&huge_count, "SEFH");
  AppendLe32(&huge_count, 107);
  AppendLe32(&huge_count, 0x10000000);
  AppendLe32(&huge_count, 12);
  AppendStr(&huge_count, "SEFT");
  RawMetadataBlock huge_count_block;
  huge_count_block.format_identifier = "container.trailer";
  huge_count_block.bytes = huge_count;
  EXPECT_FALSE(provider.DecodeToSemantic(huge_count_block, &proto, &url, &err));

  // SEF record with oversized key_len (> 256 and 0xFFFFFFF8) is safely skipped.
  std::vector<uint8_t> oversized_key_trailer =
      BuildSefTrailer(107, {{0x0A30, std::string(300, 'A'), {1, 2, 3, 4}}});
  RawMetadataBlock oversized_key_block;
  oversized_key_block.format_identifier = "container.trailer";
  oversized_key_block.bytes = oversized_key_trailer;
  ASSERT_TRUE(
      provider.DecodeToSemantic(oversized_key_block, &proto, &url, &err));
  SamsungSefMetadata oversized_meta;
  ASSERT_TRUE(oversized_meta.ParseFromArray(proto.data(), proto.size()));
  EXPECT_EQ(oversized_meta.sef_keys_size(), 0);
  EXPECT_FALSE(oversized_meta.has_motion_photo_data());
}

}  // namespace
}  // namespace motion_photo
}  // namespace libmotionphoto
