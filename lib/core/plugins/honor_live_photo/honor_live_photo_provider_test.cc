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

#include "plugins/honor_live_photo/honor_live_photo_provider.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "honor_live_photo_metadata.pb.h"  // NOLINT
#include "libmotionphoto_api.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/tests/test_framework.h"

namespace libmotionphoto {
namespace motion_photo {
namespace {

void AppendBe32(std::vector<uint8_t>* buf, uint32_t v) {
  buf->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf->push_back(static_cast<uint8_t>(v & 0xFF));
}

void AppendLe32(std::vector<uint8_t>* buf, uint32_t v) {
  buf->push_back(static_cast<uint8_t>(v & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  buf->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void AppendStr(std::vector<uint8_t>* buf, const std::string& s) {
  buf->insert(buf->end(), s.begin(), s.end());
}

void AppendTag20(std::vector<uint8_t>* buf, const std::string& tag,
                 char pad_char = '\0') {
  std::string padded = tag;
  padded.resize(20, pad_char);
  AppendStr(buf, padded);
}

void AppendHonorTlvRecord(std::vector<uint8_t>* buf,
                          const std::vector<uint8_t>& payload,
                          const std::string& tag) {
  buf->insert(buf->end(), payload.begin(), payload.end());
  AppendLe32(buf, static_cast<uint32_t>(payload.size()));
  AppendTag20(buf, tag, '\0');
}

std::vector<uint8_t> BuildFakeMp4Payload() {
  std::vector<uint8_t> mp4;
  AppendBe32(&mp4, 24);
  AppendStr(&mp4, "ftypmp42");
  AppendBe32(&mp4, 0);
  AppendStr(&mp4, "mp42isom");
  AppendBe32(&mp4, 16);
  AppendStr(&mp4, "mdat");
  AppendStr(&mp4, "HONORMP4");
  return mp4;
}

TEST(HonorLivePhotoProviderTest, IdentifiesHiHonorLivePhotoAndLiveFooters) {
  HonorLivePhotoProvider provider;
  EXPECT_EQ(provider.GetPluginName(), "honor_live_photo");

  RawMetadataBlock hihonor_block;
  hihonor_block.format_identifier = "container.trailer";
  AppendHonorTlvRecord(&hihonor_block.bytes, {'1', '2', '3', '4'},
                       "HiHonor_CIID");
  EXPECT_TRUE(provider.Identify(hihonor_block));

  RawMetadataBlock livephoto_block;
  livephoto_block.format_identifier = "container.trailer";
  AppendHonorTlvRecord(&livephoto_block.bytes, {'1', '0', '0', '0'},
                       "LivePhoto_1000");
  EXPECT_TRUE(provider.Identify(livephoto_block));

  RawMetadataBlock live_footer_block;
  live_footer_block.format_identifier = "container.trailer";
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();
  live_footer_block.bytes = mp4;
  AppendTag20(&live_footer_block.bytes,
              "LIVE_" + std::to_string(mp4.size()), ' ');
  EXPECT_TRUE(provider.Identify(live_footer_block));

  RawMetadataBlock non_honor;
  non_honor.format_identifier = "container.trailer";
  non_honor.bytes = mp4;
  EXPECT_FALSE(provider.Identify(non_honor));
}

TEST(HonorLivePhotoProviderTest,
     DecodesMagicOsBackwardTlvChainWithAndWithoutVideo) {
  HonorLivePhotoProvider provider;

  // 1. Metadata-only Honor TLV chain (as observed on Honor MagicPad 2 YLE-W09
  // with HiHonor_OfflineData sceneMode:73, without embedded MP4).
  std::vector<uint8_t> ehdr_payload(16, 0);
  std::string json =
      R"({"common":{"jpegName":"IMG_20261001_112025","sceneMode":73}})";
  std::vector<uint8_t> offline_payload;
  AppendLe32(&offline_payload, static_cast<uint32_t>(json.size()));
  AppendStr(&offline_payload, json);
  std::vector<uint8_t> ciid_payload(40, '0');

  RawMetadataBlock meta_only_block;
  meta_only_block.format_identifier = "container.trailer";
  AppendHonorTlvRecord(&meta_only_block.bytes, ehdr_payload, "HiHonor_EhdrEn");
  AppendHonorTlvRecord(&meta_only_block.bytes, offline_payload,
                       "HiHonor_OfflineData");
  AppendHonorTlvRecord(&meta_only_block.bytes, ciid_payload, "HiHonor_CIID");

  std::vector<uint8_t> proto_bytes;
  std::string type_url;
  std::string err;
  ASSERT_TRUE(provider.DecodeToSemantic(meta_only_block, &proto_bytes,
                                        &type_url, &err));
  EXPECT_EQ(type_url,
            "type.googleapis.com/libmotionphoto.motion_photo."
            "HonorLivePhotoMetadata");

  std::string payload_str(proto_bytes.begin(), proto_bytes.end());
  // Without an embedded MP4 stream, IsMotionPhoto returns false.
  EXPECT_FALSE(provider.IsMotionPhoto(type_url, payload_str));

  HonorLivePhotoMetadata meta;
  ASSERT_TRUE(meta.ParseFromString(payload_str));
  EXPECT_EQ(meta.format_variant(), "hihonor_tlv");
  EXPECT_FALSE(meta.has_embedded_video());
  EXPECT_EQ(meta.scene_mode(), 73);
  ASSERT_EQ(meta.tlv_tags_size(), 3);
  EXPECT_EQ(meta.tlv_tags(0), "HiHonor_CIID");
  EXPECT_EQ(meta.tlv_tags(1), "HiHonor_OfflineData");
  EXPECT_EQ(meta.tlv_tags(2), "HiHonor_EhdrEn");

  // 2. Honor TLV chain containing an embedded MP4 record (HiHonor_1000000).
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();
  RawMetadataBlock video_tlv_block;
  video_tlv_block.format_identifier = "container.trailer";
  AppendHonorTlvRecord(&video_tlv_block.bytes, mp4, "HiHonor_1000000");
  AppendHonorTlvRecord(&video_tlv_block.bytes, offline_payload,
                       "HiHonor_OfflineData");
  AppendHonorTlvRecord(&video_tlv_block.bytes, ciid_payload, "HiHonor_CIID");

  proto_bytes.clear();
  ASSERT_TRUE(provider.DecodeToSemantic(video_tlv_block, &proto_bytes,
                                        &type_url, &err));
  std::string video_payload_str(proto_bytes.begin(), proto_bytes.end());
  EXPECT_TRUE(provider.IsMotionPhoto(type_url, video_payload_str));

  size_t video_offset = 999;
  size_t video_len = 0;
  ASSERT_TRUE(
      provider.GetVideoInfo(video_tlv_block, &video_offset, &video_len));
  EXPECT_EQ(video_offset, 0u);
  EXPECT_EQ(video_len, mp4.size());
}

TEST(HonorLivePhotoProviderTest, DecodesHuaweiLiveFooterAndExtractsVideo) {
  HonorLivePhotoProvider provider;
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();

  // Build trailer: [MP4][20B cover offset in ms "450                 "][20B "LIVE_40             "]
  RawMetadataBlock live_block;
  live_block.format_identifier = "container.trailer";
  live_block.bytes = mp4;
  AppendTag20(&live_block.bytes, "450", ' ');
  AppendTag20(&live_block.bytes, "LIVE_" + std::to_string(mp4.size()), ' ');

  std::vector<uint8_t> proto_bytes;
  std::string type_url;
  ASSERT_TRUE(
      provider.DecodeToSemantic(live_block, &proto_bytes, &type_url, nullptr));
  HonorLivePhotoMetadata meta;
  ASSERT_TRUE(meta.ParseFromArray(proto_bytes.data(), proto_bytes.size()));
  EXPECT_EQ(meta.format_variant(), "live_footer");
  EXPECT_TRUE(meta.has_embedded_video());
  EXPECT_EQ(meta.video_offset(), 0u);
  EXPECT_EQ(meta.video_length(), mp4.size());
  EXPECT_EQ(meta.primary_image_timestamp_us(), 450000);
}

TEST(HonorLivePhotoProviderTest, EndToEndHonorJpegSplitAndExtract) {
  std::vector<uint8_t> primary_jpeg = {0xFF, 0xD8, 0xFF, 0xD9};
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();

  std::vector<uint8_t> trailer;
  AppendHonorTlvRecord(&trailer, mp4, "HiHonor_1000000");
  AppendHonorTlvRecord(&trailer, {'0', '0', '0', '1'}, "HiHonor_CIID");

  std::vector<uint8_t> full_file = primary_jpeg;
  full_file.insert(full_file.end(), trailer.begin(), trailer.end());

  libmotionphoto::api::MotionPhotoMetadata meta;
  libmotionphoto::api::HandlerOptions opts;
  EXPECT_TRUE(libmotionphoto::api::ParseMotionPhotoFromMemory(
      full_file.data(), full_file.size(), &meta, nullptr, opts));
  EXPECT_TRUE(meta.is_motion_photo);
  EXPECT_EQ(meta.video_length, mp4.size());

  libmotionphoto::api::HandlerOptions disabled_opts;
  disabled_opts.disable_3p_plugins = true;
  libmotionphoto::api::MotionPhotoMetadata disabled_meta;
  EXPECT_FALSE(libmotionphoto::api::ParseMotionPhotoFromMemory(
      full_file.data(), full_file.size(), &disabled_meta, nullptr,
      disabled_opts));

  const std::string in_path = "/tmp/honor_test_input.jpg";
  const std::string out_img = "/tmp/honor_test_primary.jpg";
  const std::string out_vid = "/tmp/honor_test_video.mp4";
  {
    std::ofstream out(in_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(full_file.data()),
              full_file.size());
  }

  libmotionphoto::api::ExtractorParams extract_params;
  extract_params.motion_photo_filepath = in_path;
  extract_params.primary_image_output_path = out_img;
  extract_params.video_output_path = out_vid;
  ASSERT_TRUE(libmotionphoto::api::ExtractMotionPhotoFromFile(extract_params));

  std::ifstream img_in(out_img, std::ios::binary);
  std::vector<uint8_t> extracted_img((std::istreambuf_iterator<char>(img_in)),
                                     std::istreambuf_iterator<char>());
  EXPECT_TRUE(extracted_img == primary_jpeg);

  std::ifstream vid_in(out_vid, std::ios::binary);
  std::vector<uint8_t> extracted_vid((std::istreambuf_iterator<char>(vid_in)),
                                     std::istreambuf_iterator<char>());
  EXPECT_TRUE(extracted_vid == mp4);

  std::remove(in_path.c_str());
  std::remove(out_img.c_str());
  std::remove(out_vid.c_str());
}

TEST(HonorLivePhotoProviderTest, RejectsMalformedTlvAndLiveFootersSafely) {
  HonorLivePhotoProvider provider;

  // TLV record whose u32LE length exceeds the trailer size.
  RawMetadataBlock bad_tlv;
  bad_tlv.format_identifier = "container.trailer";
  AppendLe32(&bad_tlv.bytes, 0x00FFFFFF);
  AppendTag20(&bad_tlv.bytes, "HiHonor_CIID", '\0');
  EXPECT_TRUE(provider.Identify(bad_tlv));

  std::vector<uint8_t> proto;
  std::string url;
  std::string err;
  EXPECT_FALSE(provider.DecodeToSemantic(bad_tlv, &proto, &url, &err));

  // LIVE_ footer with invalid size.
  RawMetadataBlock bad_live;
  bad_live.format_identifier = "container.trailer";
  AppendTag20(&bad_live.bytes, "LIVE_9999999", ' ');
  EXPECT_TRUE(provider.Identify(bad_live));
  EXPECT_FALSE(provider.DecodeToSemantic(bad_live, &proto, &url, &err));
}

}  // namespace
}  // namespace motion_photo
}  // namespace libmotionphoto
