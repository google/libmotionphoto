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

#include "plugins/apple_live_photo/apple_live_photo_provider.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "apple_live_photo_metadata.pb.h"  // NOLINT
#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/tests/test_framework.h"

namespace libmotionphoto {
namespace motion_photo {
namespace {

namespace fs = std::filesystem;

std::vector<uint8_t> MakeBox(const std::string& type,
                             const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> box;
  uint32_t size = static_cast<uint32_t>(8 + payload.size());
  box.push_back((size >> 24) & 0xFF);
  box.push_back((size >> 16) & 0xFF);
  box.push_back((size >> 8) & 0xFF);
  box.push_back(size & 0xFF);
  box.insert(box.end(), type.begin(), type.end());
  box.insert(box.end(), payload.begin(), payload.end());
  return box;
}

void PushU32(std::vector<uint8_t>* v, uint32_t val) {
  v->push_back((val >> 24) & 0xFF);
  v->push_back((val >> 16) & 0xFF);
  v->push_back((val >> 8) & 0xFF);
  v->push_back(val & 0xFF);
}

std::vector<uint8_t> BuildAppleMovWithEdtsElst(uint32_t timescale,
                                               uint32_t empty_ticks,
                                               bool include_ftyp = true) {
  std::vector<uint8_t> mvhd_payload(24, 0);
  mvhd_payload[12] = (timescale >> 24) & 0xFF;
  mvhd_payload[13] = (timescale >> 16) & 0xFF;
  mvhd_payload[14] = (timescale >> 8) & 0xFF;
  mvhd_payload[15] = timescale & 0xFF;
  auto mvhd = MakeBox("mvhd", mvhd_payload);

  std::string key = "mdta.com.apple.quicktime.still-image-time";
  std::vector<uint8_t> mebx_payload(8, 0);
  mebx_payload.insert(mebx_payload.end(), key.begin(), key.end());
  auto mebx = MakeBox("mebx", mebx_payload);
  std::vector<uint8_t> stsd_payload(8, 0);
  stsd_payload[7] = 1;
  stsd_payload.insert(stsd_payload.end(), mebx.begin(), mebx.end());
  auto stsd = MakeBox("stsd", stsd_payload);

  std::vector<uint8_t> hdlr_payload(12, 0);
  hdlr_payload[8] = 'm';
  hdlr_payload[9] = 'e';
  hdlr_payload[10] = 't';
  hdlr_payload[11] = 'a';
  auto hdlr = MakeBox("hdlr", hdlr_payload);

  std::vector<uint8_t> mdhd_payload(24, 0);
  mdhd_payload[12] = (timescale >> 24) & 0xFF;
  mdhd_payload[13] = (timescale >> 16) & 0xFF;
  mdhd_payload[14] = (timescale >> 8) & 0xFF;
  mdhd_payload[15] = timescale & 0xFF;
  mdhd_payload[19] = 1;
  auto mdhd = MakeBox("mdhd", mdhd_payload);

  std::vector<uint8_t> elst_payload(8, 0);
  elst_payload[7] = 2;
  PushU32(&elst_payload, empty_ticks);
  PushU32(&elst_payload, 0xFFFFFFFF);  // media_time = -1 (empty edit)
  PushU32(&elst_payload, 0x00010000);
  PushU32(&elst_payload, 1);
  PushU32(&elst_payload, 0);
  PushU32(&elst_payload, 0x00010000);
  auto edts = MakeBox("edts", MakeBox("elst", elst_payload));

  auto stbl = MakeBox("stbl", stsd);
  auto minf = MakeBox("minf", stbl);
  std::vector<uint8_t> mdia_payload;
  mdia_payload.insert(mdia_payload.end(), mdhd.begin(), mdhd.end());
  mdia_payload.insert(mdia_payload.end(), hdlr.begin(), hdlr.end());
  mdia_payload.insert(mdia_payload.end(), minf.begin(), minf.end());
  auto mdia = MakeBox("mdia", mdia_payload);

  std::vector<uint8_t> tkhd_payload(24, 0);
  tkhd_payload[15] = 5;
  PushU32(&tkhd_payload, empty_ticks + 1);
  auto tkhd = MakeBox("tkhd", tkhd_payload);

  std::vector<uint8_t> trak_payload;
  trak_payload.insert(trak_payload.end(), tkhd.begin(), tkhd.end());
  trak_payload.insert(trak_payload.end(), edts.begin(), edts.end());
  trak_payload.insert(trak_payload.end(), mdia.begin(), mdia.end());
  auto trak = MakeBox("trak", trak_payload);

  std::vector<uint8_t> moov_payload = mvhd;
  moov_payload.insert(moov_payload.end(), trak.begin(), trak.end());
  auto moov = MakeBox("moov", moov_payload);
  auto wide = MakeBox("wide", {});
  auto mdat = MakeBox("mdat", {0xFF});

  std::vector<uint8_t> mov;
  if (include_ftyp) {
    std::vector<uint8_t> ftyp_payload = {'q', 't', ' ', ' ', 0, 0, 0, 0,
                                         'q', 't', ' ', ' ', 0, 0, 0, 0};
    auto ftyp = MakeBox("ftyp", ftyp_payload);
    mov.insert(mov.end(), ftyp.begin(), ftyp.end());
  }
  mov.insert(mov.end(), wide.begin(), wide.end());
  mov.insert(mov.end(), mdat.begin(), mdat.end());
  mov.insert(mov.end(), moov.begin(), moov.end());
  return mov;
}

TEST(AppleLivePhotoProviderTest, IdentifiesAndDecodesEdtsElstAndHeaderlessMov) {
  AppleLivePhotoProvider provider;
  EXPECT_EQ(provider.GetPluginName(), "apple_live_photo");

  // 1. Standard Apple .mov with ftyp + wide + mdat + moov (840 ticks @ 600 Hz
  // = 1,400,000 us)
  auto mov_with_ftyp = BuildAppleMovWithEdtsElst(600, 840, true);
  RawMetadataBlock block;
  block.type = "PROPRIETARY";
  block.format_identifier = "container.trailer";
  block.data_view = mov_with_ftyp.data();
  block.data_view_size = mov_with_ftyp.size();
  block.total_payload_size = mov_with_ftyp.size();

  EXPECT_TRUE(provider.Identify(block));

  std::vector<uint8_t> proto_bytes;
  std::string type_url;
  std::string err;
  EXPECT_TRUE(
      provider.DecodeToSemantic(block, &proto_bytes, &type_url, &err));
  EXPECT_EQ(
      type_url,
      "type.googleapis.com/libmotionphoto.motion_photo.AppleLivePhotoMetadata");

  std::string payload(reinterpret_cast<const char*>(proto_bytes.data()),
                      proto_bytes.size());
  EXPECT_TRUE(provider.IsMotionPhoto(type_url, payload));

  AppleLivePhotoMetadata decoded;
  ASSERT_TRUE(decoded.ParseFromString(payload));
  EXPECT_TRUE(decoded.has_still_image_time_track());
  EXPECT_EQ(decoded.primary_image_timestamp_us(), 1400000);
  EXPECT_EQ(decoded.movie_timescale(), 600u);
  EXPECT_EQ(decoded.video_offset(), 0u);
  EXPECT_EQ(decoded.video_length(), mov_with_ftyp.size());

  size_t vid_off = 99;
  size_t vid_len = 0;
  EXPECT_TRUE(provider.GetVideoInfo(block, &vid_off, &vid_len));
  EXPECT_EQ(vid_off, 0u);
  EXPECT_EQ(vid_len, mov_with_ftyp.size());

  // 2. Headerless Apple .mov ([wide][mdat][moov], like IMG_0002.MOV)
  auto mov_headerless = BuildAppleMovWithEdtsElst(600, 800, false);
  EXPECT_EQ(AppleLivePhotoProvider::ExtractStillImageTimestampUs(
                mov_headerless.data(), mov_headerless.size()),
            1333333);

  const std::string tmp_mov = "/tmp/apple_live_photo_provider_test.mov";
  {
    std::ofstream out(tmp_mov, std::ios::binary);
    out.write(reinterpret_cast<const char*>(mov_headerless.data()),
              mov_headerless.size());
  }
  EXPECT_EQ(
      AppleLivePhotoProvider::ExtractStillImageTimestampUsFromFile(tmp_mov),
      1333333);
  fs::remove(tmp_mov);
}

TEST(AppleLivePhotoProviderTest, RejectsNonAppleMp4AndTruncatedStreams) {
  AppleLivePhotoProvider provider;
  EXPECT_EQ(AppleLivePhotoProvider::ExtractStillImageTimestampUsFromFile(
                "lib/core/motion_photo/testdata/motion_photo_video_only.mp4"),
            -1);

  std::vector<uint8_t> short_buf = {0x00, 0x00, 0x00, 0x08, 'w', 'i', 'd', 'e'};
  RawMetadataBlock block;
  block.type = "PROPRIETARY";
  block.format_identifier = "container.trailer";
  block.data_view = short_buf.data();
  block.data_view_size = short_buf.size();
  block.total_payload_size = short_buf.size();
  EXPECT_FALSE(provider.Identify(block));
}

}  // namespace
}  // namespace motion_photo
}  // namespace libmotionphoto
