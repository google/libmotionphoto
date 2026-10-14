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

#include "plugins/oplus_olive/oplus_olive_provider.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "libmotionphoto_api.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/tests/test_framework.h"
#include "oplus_olive_metadata.pb.h"  // NOLINT

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

std::vector<uint8_t> BuildFakeMp4Payload() {
  std::vector<uint8_t> mp4;
  AppendBe32(&mp4, 24);
  AppendStr(&mp4, "ftypmp42");
  AppendBe32(&mp4, 0);
  AppendStr(&mp4, "mp42isom");
  AppendBe32(&mp4, 16);
  AppendStr(&mp4, "mdat");
  AppendStr(&mp4, "OPLUSMP4");
  return mp4;
}

// Appends the 68-byte Oplus "jxrs" integrity footer:
// [60-byte hex digest]["jxrs"][u32le version]
std::vector<uint8_t> BuildOplusJxrsTrailer(const std::vector<uint8_t>& mp4,
                                           const std::string& hex_60,
                                           uint32_t version = 2) {
  std::vector<uint8_t> trailer = mp4;
  std::string padded_hex = hex_60;
  padded_hex.resize(60, '0');
  AppendStr(&trailer, padded_hex);
  AppendStr(&trailer, "jxrs");
  AppendLe32(&trailer, version);
  return trailer;
}

std::vector<uint8_t> BuildJpegWithApp1Xmp(const std::string& xmp_xml) {
  std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE1};
  const std::string kXmpHeader = "http://ns.adobe.com/xap/1.0/\0";
  uint16_t seg_len = static_cast<uint16_t>(2 + 29 + xmp_xml.size());
  jpeg.push_back(static_cast<uint8_t>((seg_len >> 8) & 0xFF));
  jpeg.push_back(static_cast<uint8_t>(seg_len & 0xFF));
  jpeg.insert(jpeg.end(), kXmpHeader.data(), kXmpHeader.data() + 29);
  AppendStr(&jpeg, xmp_xml);
  jpeg.push_back(0xFF);
  jpeg.push_back(0xD9);
  return jpeg;
}

TEST(OplusOliveProviderTest, IdentifiesXmpAndJxrsTrailer) {
  OplusOliveProvider provider;
  EXPECT_EQ(provider.GetPluginName(), "oplus_olive");

  RawMetadataBlock xmp_block;
  xmp_block.type = "XMP";
  xmp_block.format_identifier = "standard.xmp";
  std::string xmp_str =
      R"(<rdf:Description xmlns:OpCamera="http://ns.oplus.com/photo/1.0/camera/" OpCamera:OLivePhotoVersion="2"/>)";
  xmp_block.bytes.assign(xmp_str.begin(), xmp_str.end());
  EXPECT_TRUE(provider.Identify(xmp_block));

  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();
  std::string hex =
      "498f06a9ba719d0f2a0ab713ab1d66be2f6c5360b44e6993ec0a03a35197";
  RawMetadataBlock jxrs_trailer;
  jxrs_trailer.type = "PROPRIETARY";
  jxrs_trailer.format_identifier = "container.trailer";
  jxrs_trailer.bytes = BuildOplusJxrsTrailer(mp4, hex, 2);
  EXPECT_TRUE(provider.Identify(jxrs_trailer));

  RawMetadataBlock non_oplus_trailer;
  non_oplus_trailer.format_identifier = "container.trailer";
  non_oplus_trailer.bytes = mp4;
  EXPECT_FALSE(provider.Identify(non_oplus_trailer));
}

TEST(OplusOliveProviderTest, DecodesJxrsTrailerAndStrips68ByteFooter) {
  OplusOliveProvider provider;
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();
  std::string hex =
      "498f06a9ba719d0f2a0ab713ab1d66be2f6c5360b44e6993ec0a03a35197";

  RawMetadataBlock block;
  block.type = "PROPRIETARY";
  block.format_identifier = "container.trailer";
  block.bytes = BuildOplusJxrsTrailer(mp4, hex, 2);

  std::vector<uint8_t> proto_bytes;
  std::string type_url;
  std::string err;
  ASSERT_TRUE(provider.DecodeToSemantic(block, &proto_bytes, &type_url, &err));
  EXPECT_EQ(
      type_url,
      "type.googleapis.com/libmotionphoto.motion_photo.OplusOliveMetadata");

  std::string payload_str(proto_bytes.begin(), proto_bytes.end());
  EXPECT_TRUE(provider.IsMotionPhoto(type_url, payload_str));

  OplusOliveMetadata meta;
  ASSERT_TRUE(meta.ParseFromString(payload_str));
  EXPECT_EQ(meta.olive_photo_version(), 2);
  EXPECT_TRUE(meta.has_jxrs_footer());
  EXPECT_EQ(meta.jxrs_footer_version(), 2u);
  EXPECT_EQ(meta.jxrs_checksum_hex(), hex);
  EXPECT_EQ(meta.video_offset(), 0u);
  EXPECT_EQ(meta.video_length(), mp4.size());

  size_t video_offset = 99;
  size_t video_len = 0;
  ASSERT_TRUE(provider.GetVideoInfo(block, &video_offset, &video_len));
  EXPECT_EQ(video_offset, 0u);
  EXPECT_EQ(video_len, mp4.size());
}

TEST(OplusOliveProviderTest, DecodesJxrsJsonDirectoryAndStripsSubVideoTrailer) {
  OplusOliveProvider provider;
  std::vector<uint8_t> primary_mp4 = BuildFakeMp4Payload();

  // Simulate OPPO Reno15 Pro Max / OnePlus 11 jxrs trailer containing
  // trailing records (e.g., live.subVideo + capture.mode) indexed by a
  // backward-offset JSON array before "jxrs" + u32LE(json_plus_null_len).
  std::vector<uint8_t> sub_video(120, 'S');
  std::vector<uint8_t> capture_mode(32, 'C');
  std::string json_dir =
      R"([{"length":120,"name":"live.subVideo","offset":152,"version":1},{"length":32,"name":"capture.mode","offset":32,"version":1}])";
  json_dir.push_back('\0');

  RawMetadataBlock block;
  block.type = "PROPRIETARY";
  block.format_identifier = "container.trailer";
  block.bytes = primary_mp4;
  block.bytes.insert(block.bytes.end(), sub_video.begin(), sub_video.end());
  block.bytes.insert(block.bytes.end(), capture_mode.begin(),
                     capture_mode.end());
  AppendStr(&block.bytes, json_dir);
  AppendStr(&block.bytes, "jxrs");
  AppendLe32(&block.bytes, static_cast<uint32_t>(json_dir.size()));

  size_t video_offset = 999;
  size_t video_len = 0;
  ASSERT_TRUE(provider.GetVideoInfo(block, &video_offset, &video_len));
  EXPECT_EQ(video_offset, 0u);
  EXPECT_EQ(video_len, primary_mp4.size());
}

TEST(OplusOliveProviderTest, DecodesPureOpCameraXmp) {
  OplusOliveProvider provider;
  RawMetadataBlock xmp_block;
  xmp_block.type = "XMP";
  xmp_block.format_identifier = "standard.xmp";
  std::string xmp_str =
      R"(<rdf:Description xmlns:OpCamera="http://ns.oppo.com/photo/1.0/camera/" OpCamera:OLivePhotoVersion="1" OpCamera:VideoLength="4096" OpCamera:PrimaryPresentationTimestampUs="250000"/>)";
  xmp_block.bytes.assign(xmp_str.begin(), xmp_str.end());

  std::vector<uint8_t> proto_bytes;
  std::string type_url;
  ASSERT_TRUE(
      provider.DecodeToSemantic(xmp_block, &proto_bytes, &type_url, nullptr));
  OplusOliveMetadata meta;
  ASSERT_TRUE(meta.ParseFromArray(proto_bytes.data(), proto_bytes.size()));
  EXPECT_EQ(meta.olive_photo_version(), 1);
  EXPECT_EQ(meta.video_length(), 4096u);
  EXPECT_EQ(meta.primary_image_timestamp_us(), 250000);
}

TEST(OplusOliveProviderTest, EndToEndOplusJpegExtractsCleanMp4WithoutJxrsTail) {
  std::string xmp =
      R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description xmlns:OpCamera="http://ns.oplus.com/photo/1.0/camera/" OpCamera:OLivePhotoVersion="2"/></rdf:RDF></x:xmpmeta>)";
  std::vector<uint8_t> primary_jpeg = BuildJpegWithApp1Xmp(xmp);
  std::vector<uint8_t> mp4 = BuildFakeMp4Payload();
  std::string hex =
      "c229bc5601383de06071c2d414da2d2ca2a82e812fa665e2087a6a194b7b";
  std::vector<uint8_t> trailer = BuildOplusJxrsTrailer(mp4, hex, 2);

  std::vector<uint8_t> full_file = primary_jpeg;
  full_file.insert(full_file.end(), trailer.begin(), trailer.end());

  libmotionphoto::api::MotionPhotoMetadata meta;
  libmotionphoto::api::HandlerOptions opts;
  EXPECT_TRUE(libmotionphoto::api::ParseMotionPhotoFromMemory(
      full_file.data(), full_file.size(), &meta, nullptr, opts));
  EXPECT_TRUE(meta.is_motion_photo);
  // Video length must exclude the 68-byte jxrs footer!
  EXPECT_EQ(meta.video_length, mp4.size());

  // When 3P plugins are disabled, pure OpCamera file is not a 1P Motion Photo.
  libmotionphoto::api::HandlerOptions disabled_opts;
  disabled_opts.disable_3p_plugins = true;
  libmotionphoto::api::MotionPhotoMetadata disabled_meta;
  EXPECT_FALSE(libmotionphoto::api::ParseMotionPhotoFromMemory(
      full_file.data(), full_file.size(), &disabled_meta, nullptr,
      disabled_opts));

  // Verify primary image and video track extraction.
  const std::string in_path = "/tmp/oplus_test_input.jpg";
  const std::string out_img = "/tmp/oplus_test_primary.jpg";
  const std::string out_vid = "/tmp/oplus_test_video.mp4";
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

TEST(OplusOliveProviderTest, RejectsNonMp4JxrsTrailerSafely) {
  OplusOliveProvider provider;
  RawMetadataBlock bad_trailer;
  bad_trailer.format_identifier = "container.trailer";
  bad_trailer.bytes.resize(80, 'X');
  std::memcpy(bad_trailer.bytes.data() + 72, "jxrs", 4);

  EXPECT_TRUE(provider.Identify(bad_trailer));
  std::vector<uint8_t> proto;
  std::string url;
  std::string err;
  EXPECT_FALSE(provider.DecodeToSemantic(bad_trailer, &proto, &url, &err));
}

}  // namespace
}  // namespace motion_photo
}  // namespace libmotionphoto
