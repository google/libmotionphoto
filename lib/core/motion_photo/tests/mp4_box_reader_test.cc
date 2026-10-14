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

#include "motion_photo_mp4/mp4_box_reader.h"

#include <fstream>
#include <string>
#include <vector>

#include "motion_photo/agtm.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/tests/test_framework.h"
#include "motion_photo/video_metadata.h"
#include "motion_photo_mp4/motion_photo_mp4_file_reader.h"

namespace libmotionphoto {
namespace motion_photo {
namespace {

TEST(Mp4BoxReaderTest, ParseMp4VideoOnly) {
  const std::string kFile = "lib/core/motion_photo/testdata/motion_photo_video_only.mp4";
  Mp4BoxReader reader;
  EXPECT_TRUE(reader.ParseFromFile(kFile, 0));
  EXPECT_EQ(reader.GetTrackCount(), 2);

  const auto& video_track = reader.GetTrack(0);
  EXPECT_EQ(video_track.track_id, 1);
  EXPECT_EQ(video_track.handler_type, "vide");
  EXPECT_EQ(video_track.sample_count, 186);

  const auto& audio_track = reader.GetTrack(1);
  EXPECT_EQ(audio_track.track_id, 2);
  EXPECT_EQ(audio_track.handler_type, "soun");
  EXPECT_EQ(audio_track.sample_count, 133);
}

TEST(Mp4BoxReaderTest, ParseMp4VideoWithHighResAndMetadata) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_video_only_with_high_res.mp4";
  Mp4BoxReader reader;
  EXPECT_TRUE(reader.ParseFromFile(kFile, 0));
  EXPECT_EQ(reader.GetTrackCount(), 4);

  const Mp4TrackInfo* meta_track = reader.FindTrackById(4);
  ASSERT_NE(meta_track, nullptr);
  EXPECT_EQ(meta_track->handler_type, "meta");
  EXPECT_EQ(meta_track->sample_count, 1);
  EXPECT_TRUE(Mp4BoxReader::TrackHasMettMime(*meta_track, kMotionPhotoMetadataMime));

  std::vector<uint8_t> sample;
  EXPECT_TRUE(reader.ReadSample(4, 1, &sample));
  EXPECT_EQ(sample.size(), 101);
}

TEST(Mp4BoxReaderTest, ParseJpegMotionPhotoAtOffset) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.jpg";
  MotionPhotoMp4FileReader mp4_reader;
  EXPECT_TRUE(mp4_reader.Read(kFile, 30895));

  const auto& track_data = mp4_reader.GetVideoTrackData();
  EXPECT_EQ(track_data.video_track_count, 2);
  EXPECT_EQ(track_data.audio_track_count, 1);
  EXPECT_EQ(track_data.metadata_track_count, 2);
  EXPECT_EQ(track_data.low_res_video_track_id, 1);
  EXPECT_EQ(track_data.low_res_audio_track_id, 2);
  EXPECT_EQ(track_data.high_res_video_track_id, 3);
  EXPECT_EQ(track_data.high_res_metadata_track_id, 4);

  const auto& meta_bytes = mp4_reader.GetHighResTrackVideoMetadata();
  EXPECT_EQ(meta_bytes.size(), 101);
}

TEST(Mp4BoxReaderTest, ParseHeicMotionPhotoAtOffset) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_single_video_track.MP.heic";
  MotionPhotoMp4FileReader mp4_reader;
  // mpvd box header is 16 bytes at offset 28727, so MP4 starts at 28743
  EXPECT_TRUE(mp4_reader.Read(kFile, 28743));

  const auto& track_data = mp4_reader.GetVideoTrackData();
  EXPECT_EQ(track_data.video_track_count, 2);
  EXPECT_EQ(track_data.audio_track_count, 1);
  EXPECT_EQ(track_data.metadata_track_count, 2);
  EXPECT_EQ(track_data.high_res_metadata_track_id, 4);

  const auto& meta_bytes = mp4_reader.GetHighResTrackVideoMetadata();
  EXPECT_EQ(meta_bytes.size(), 101);
}

TEST(Mp4BoxReaderTest, ExtractAgtmFromRealAgtmMotionPhoto) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_agtm.MP.jpg";
  MetadataEngine engine(nullptr);
  std::string json = engine.ExtractAgtmAtTimestamp(kFile, 0);
  EXPECT_FALSE(json.empty());
  EXPECT_NE(json.find("hdrReferenceWhite"), std::string::npos);
}

TEST(Mp4BoxReaderTest, ExtractAgtmFromMemoryRealAgtmMotionPhoto) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_agtm.MP.jpg";
  std::ifstream ifs(kFile, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(ifs.is_open());
  size_t size = ifs.tellg();
  ifs.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  ifs.read(reinterpret_cast<char*>(buffer.data()), size);

  MetadataEngine engine(nullptr);
  std::string json =
      engine.ExtractAgtmAtTimestampMemory(buffer.data(), buffer.size(), 0);
  EXPECT_FALSE(json.empty());
  EXPECT_NE(json.find("hdrReferenceWhite"), std::string::npos);
}

TEST(Mp4BoxReaderTest, ExtractAgtmFromNonAgtmPhotoReturnsEmpty) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_no_agtm.MP.jpg";
  MetadataEngine engine(nullptr);
  std::string json = engine.ExtractAgtmAtTimestamp(kFile, 0);
  EXPECT_TRUE(json.empty());
}

TEST(Mp4BoxReaderTest, ExtractAgtmFromNonAgtmVideoWithMetadataReturnsEmpty) {
  const std::string kFile =
      "lib/core/motion_photo/testdata/motion_photo_video_only_with_high_res.mp4";
  MetadataEngine engine(nullptr);
  std::string json = engine.ExtractAgtmAtTimestamp(kFile, 0);
  EXPECT_TRUE(json.empty());
}

TEST(Mp4BoxReaderTest, ParseAgtmPayloadDirectBitstream) {
  // 1. Raw default ST 2094-50 2-byte bitstream (app_version=0, min_app_version=0, no tone map)
  std::vector<uint8_t> default_bytes = {0x00, 0x00};
  AgtmDynamicMetadata meta;
  EXPECT_TRUE(ParseAgtmPayload(default_bytes.data(), default_bytes.size(), &meta));
  EXPECT_EQ(meta.hdr_reference_white, 203.0f);
  EXPECT_FALSE(meta.has_adaptive_tone_map_flag);

  // 2. Wrapped with ITU-T T.35 header
  std::vector<uint8_t> t35_bytes = {0x05, 0xB5, 0x00, 0x90, 0x00, 0x01, 0x00, 0x00};
  EXPECT_TRUE(ParseAgtmPayload(t35_bytes.data(), t35_bytes.size(), &meta));
  EXPECT_EQ(meta.hdr_reference_white, 203.0f);

  // 3. Invalid payload (random garbage without T.35 header and invalid version)
  std::vector<uint8_t> invalid_bytes = {0xFF, 0xFF, 0x12, 0x34};
  EXPECT_FALSE(ParseAgtmPayload(invalid_bytes.data(), invalid_bytes.size(), &meta));
}

TEST(Mp4BoxReaderTest, ParseMp4WithMalformedTableEntryCountsDoesNotOom) {
  auto make_box = [](const std::string& type,
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
  };

  auto make_trak = [&](uint32_t track_id,
                       const std::vector<uint8_t>& stbl_children) {
    auto stbl = make_box("stbl", stbl_children);
    auto minf = make_box("minf", stbl);
    auto mdia = make_box("mdia", minf);

    std::vector<uint8_t> tkhd_payload(24, 0);
    tkhd_payload[15] = static_cast<uint8_t>(track_id);
    auto tkhd = make_box("tkhd", tkhd_payload);

    std::vector<uint8_t> trak_payload = tkhd;
    trak_payload.insert(trak_payload.end(), mdia.begin(), mdia.end());
    return make_box("trak", trak_payload);
  };

  // Track 1: stsz (uniform_sample_size=0, sample_count=0x7fffffff, 0 entries),
  //          stsc (count=0x7fffffff, 0 entries),
  //          stco (count=0x7fffffff, 0 entries).
  std::vector<uint8_t> stsz_empty_payload = {
      0x00, 0x00, 0x00, 0x00,  // version + flags
      0x00, 0x00, 0x00, 0x00,  // uniform_sample_size = 0
      0x7F, 0xFF, 0xFF, 0xFF   // sample_count = 0x7fffffff
  };
  std::vector<uint8_t> count_max_payload = {
      0x00, 0x00, 0x00, 0x00,  // version + flags
      0x7F, 0xFF, 0xFF, 0xFF   // count = 0x7fffffff
  };
  auto stsz1 = make_box("stsz", stsz_empty_payload);
  auto stsc1 = make_box("stsc", count_max_payload);
  auto stco1 = make_box("stco", count_max_payload);
  std::vector<uint8_t> stbl1_children;
  stbl1_children.insert(stbl1_children.end(), stsz1.begin(), stsz1.end());
  stbl1_children.insert(stbl1_children.end(), stsc1.begin(), stsc1.end());
  stbl1_children.insert(stbl1_children.end(), stco1.begin(), stco1.end());
  auto trak1 = make_trak(1, stbl1_children);

  // Track 2: co64 (count=0x7fffffff, 0 entries).
  auto co64_2 = make_box("co64", count_max_payload);
  auto trak2 = make_trak(2, co64_2);

  // Track 3: stsz with uniform_sample_size=0, sample_count=0x7fffffff, and
  //          only 1 actual sample size entry (4 bytes), plus 1 valid stsc and
  //          1 valid stco entry. Verifies sample_count is clamped to
  //          sample_sizes.size() so ReadSample(3, 2, ...) does not read OOB.
  std::vector<uint8_t> stsz_one_payload = {
      0x00, 0x00, 0x00, 0x00,  // version + flags
      0x00, 0x00, 0x00, 0x00,  // uniform_sample_size = 0
      0x7F, 0xFF, 0xFF, 0xFF,  // sample_count = 0x7fffffff (spoofed)
      0x00, 0x00, 0x00, 0x04   // 1 actual entry: sample_size = 4
  };
  std::vector<uint8_t> stsc_one_payload = {
      0x00, 0x00, 0x00, 0x00,  // version + flags
      0x00, 0x00, 0x00, 0x01,  // count = 1
      0x00, 0x00, 0x00, 0x01,  // first_chunk = 1
      0x00, 0x00, 0x00, 0x02,  // samples_per_chunk = 2
      0x00, 0x00, 0x00, 0x01   // sample_description_index = 1
  };
  std::vector<uint8_t> stco_one_payload = {
      0x00, 0x00, 0x00, 0x00,  // version + flags
      0x00, 0x00, 0x00, 0x01,  // count = 1
      0x00, 0x00, 0x00, 0x00   // chunk_offset = 0
  };
  auto stsz3 = make_box("stsz", stsz_one_payload);
  auto stsc3 = make_box("stsc", stsc_one_payload);
  auto stco3 = make_box("stco", stco_one_payload);
  std::vector<uint8_t> stbl3_children;
  stbl3_children.insert(stbl3_children.end(), stsz3.begin(), stsz3.end());
  stbl3_children.insert(stbl3_children.end(), stsc3.begin(), stsc3.end());
  stbl3_children.insert(stbl3_children.end(), stco3.begin(), stco3.end());
  auto trak3 = make_trak(3, stbl3_children);

  std::vector<uint8_t> moov_payload;
  moov_payload.insert(moov_payload.end(), trak1.begin(), trak1.end());
  moov_payload.insert(moov_payload.end(), trak2.begin(), trak2.end());
  moov_payload.insert(moov_payload.end(), trak3.begin(), trak3.end());
  auto moov = make_box("moov", moov_payload);

  std::vector<uint8_t> ftyp_payload = {'i', 's', 'o', 'm', 0,   0,   2,   0,
                                       'i', 's', 'o', 'm', 'm', 'p', '4', '1'};
  auto ftyp = make_box("ftyp", ftyp_payload);

  std::vector<uint8_t> mp4 = ftyp;
  mp4.insert(mp4.end(), moov.begin(), moov.end());

  Mp4BoxReader reader;
  EXPECT_TRUE(reader.ParseFromMemory(mp4.data(), mp4.size(), 0));

  const Mp4TrackInfo* track1 = reader.FindTrackById(1);
  ASSERT_NE(track1, nullptr);
  EXPECT_EQ(track1->sample_count, 0);
  EXPECT_EQ(track1->sample_sizes.size(), 0);
  EXPECT_EQ(track1->sample_sizes.capacity(), 0);
  EXPECT_EQ(track1->stsc_entries.size(), 0);
  EXPECT_EQ(track1->stsc_entries.capacity(), 0);
  EXPECT_EQ(track1->chunk_offsets.size(), 0);
  EXPECT_EQ(track1->chunk_offsets.capacity(), 0);

  const Mp4TrackInfo* track2 = reader.FindTrackById(2);
  ASSERT_NE(track2, nullptr);
  EXPECT_EQ(track2->chunk_offsets.size(), 0);
  EXPECT_EQ(track2->chunk_offsets.capacity(), 0);

  const Mp4TrackInfo* track3 = reader.FindTrackById(3);
  ASSERT_NE(track3, nullptr);
  EXPECT_EQ(track3->sample_sizes.size(), 1);
  EXPECT_EQ(track3->sample_sizes.capacity(), 1);
  EXPECT_EQ(track3->sample_count, 1);
  std::vector<uint8_t> sample;
  EXPECT_TRUE(reader.ReadSample(3, 1, &sample));
  EXPECT_FALSE(reader.ReadSample(3, 2, &sample));
}

TEST(Mp4BoxReaderTest, TrackScoreDescriptorWithMalformedFrameCountDoesNotOom) {
  // TrackScoreDescriptor (tag = 0xC2, instance_size = 4) with
  // frame_score_count = 0x7fffffff and 0 FrameScoreDescriptor bytes remaining.
  std::vector<uint8_t> bytes = {
      TrackScoreDescriptor::kTag,
      0x04,                    // instance_size = 4 bytes
      0x7F, 0xFF, 0xFF, 0xFF   // frame_score_count = 0x7fffffff
  };
  image_io::IsoDecoder decoder(bytes.data(), 0, bytes.size());
  TrackScoreDescriptor descriptor;
  descriptor.DecodeTagInstanceSizeAndDetails(&decoder);
  EXPECT_TRUE(decoder.HasErrors());
  EXPECT_EQ(descriptor.GetNumScoredFrames(), 0);
  EXPECT_EQ(descriptor.GetFrameScoreDescriptors().size(), 0);
  EXPECT_EQ(descriptor.GetFrameScoreDescriptors().capacity(), 0);
}

}  // namespace
}  // namespace motion_photo
}  // namespace libmotionphoto
