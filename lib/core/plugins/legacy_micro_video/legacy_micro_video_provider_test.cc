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

#include "plugins/legacy_micro_video/legacy_micro_video_provider.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "image_io/base/message_handler.h"
#include "image_io/base/string_ref_data_source.h"
#include "legacy_micro_video_metadata.pb.h"
#include "motion_photo/google_motion_photo_provider.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/metadata_provider_registry.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/tests/test_framework.h"

namespace libmotionphoto {
namespace motion_photo {

TEST(LegacyMicroVideoProviderTest, IdentifyXmpAndTrailer) {
  LegacyMicroVideoProvider legacy_provider;
  GoogleMotionPhotoProvider google_provider;

  RawMetadataBlock legacy_xmp_block;
  legacy_xmp_block.type = "XMP";
  legacy_xmp_block.format_identifier = "standard.xmp";
  std::string legacy_xmp_data =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "GCamera:MicroVideo='1' GCamera:MicroVideoVersion='1' "
      "GCamera:MicroVideoOffset='4261545'/></rdf:RDF></x:xmpmeta>";
  legacy_xmp_block.bytes.assign(legacy_xmp_data.begin(), legacy_xmp_data.end());

  EXPECT_TRUE(legacy_provider.Identify(legacy_xmp_block));
  EXPECT_FALSE(google_provider.Identify(legacy_xmp_block));

  RawMetadataBlock google_motion_photo_xmp_block;
  google_motion_photo_xmp_block.type = "XMP";
  google_motion_photo_xmp_block.format_identifier = "standard.xmp";
  std::string google_mp_data =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "xmlns:Container='http://ns.google.com/photos/1.0/container/' "
      "GCamera:MotionPhoto='1' GCamera:MotionPhotoVersion='1' "
      "GCamera:MotionPhotoPresentationTimestampUs='500000'/></rdf:RDF></"
      "x:xmpmeta>";
  google_motion_photo_xmp_block.bytes.assign(google_mp_data.begin(),
                                             google_mp_data.end());

  EXPECT_TRUE(google_provider.Identify(google_motion_photo_xmp_block));

  RawMetadataBlock container_only_xmp_block;
  container_only_xmp_block.type = "XMP";
  container_only_xmp_block.format_identifier = "standard.xmp";
  std::string container_only_data =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description "
      "xmlns:Container='http://ns.google.com/photos/1.0/container/' "
      "Container:Version='1'/></rdf:RDF></x:xmpmeta>";
  container_only_xmp_block.bytes.assign(container_only_data.begin(),
                                        container_only_data.end());

  EXPECT_FALSE(google_provider.Identify(container_only_xmp_block));

  RawMetadataBlock trailer_block;
  trailer_block.type = "PROPRIETARY";
  trailer_block.format_identifier = "container.trailer";
  std::string trailer_data = "    ftypmp42...";
  trailer_block.bytes.assign(trailer_data.begin(), trailer_data.end());

  EXPECT_TRUE(legacy_provider.Identify(trailer_block));
}

TEST(LegacyMicroVideoProviderTest, DecodeToSemantic) {
  LegacyMicroVideoProvider provider;

  RawMetadataBlock xmp_block;
  xmp_block.type = "XMP";
  xmp_block.format_identifier = "standard.xmp";
  std::string xmp_data =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "GCamera:MicroVideo='1' GCamera:MicroVideoVersion='1' "
      "GCamera:MicroVideoPresentationTimestampUs='1475413' "
      "GCamera:MicroVideoOffset='4261545'/></rdf:RDF></x:xmpmeta>";
  xmp_block.bytes.assign(xmp_data.begin(), xmp_data.end());

  std::vector<uint8_t> proto_bytes;
  std::string type_url;
  std::string error_message;

  EXPECT_TRUE(provider.DecodeToSemantic(xmp_block, &proto_bytes, &type_url,
                                        &error_message));
  EXPECT_EQ(type_url,
            "type.googleapis.com/libmotionphoto.motion_photo."
            "LegacyMicroVideoMetadata");

  LegacyMicroVideoMetadata metadata;
  std::string payload_str(reinterpret_cast<char*>(proto_bytes.data()),
                          proto_bytes.size());
  EXPECT_TRUE(metadata.ParseFromString(payload_str));
  EXPECT_EQ(metadata.legacy_micro_video(), 1);
  EXPECT_EQ(metadata.primary_image_timestamp_us(), 1475413);
  EXPECT_EQ(metadata.version(), 1);

  EXPECT_TRUE(provider.IsMotionPhoto(type_url, payload_str));
}

TEST(LegacyMicroVideoProviderTest, GetVideoInfo) {
  LegacyMicroVideoProvider provider;

  RawMetadataBlock trailer_block;
  trailer_block.type = "PROPRIETARY";
  trailer_block.format_identifier = "container.trailer";
  trailer_block.bytes.resize(4261545);

  size_t offset_in_block = 0;
  size_t length = 0;
  EXPECT_TRUE(provider.GetVideoInfo(trailer_block, &offset_in_block, &length));
  EXPECT_EQ(offset_in_block, 0);
  EXPECT_EQ(length, 4261545);
}

TEST(LegacyMicroVideoProviderTest, MetadataEngineIntegration) {
  image_io::MessageHandler message_handler;
  MetadataEngine engine(&message_handler);
  engine.RegisterProvider(std::make_unique<LegacyMicroVideoProvider>());

  // Construct synthetic micro video file in memory
  // 1. JPEG with XMP containing GCamera:MicroVideo='1'
  // 2. Trailing MP4 payload
  std::string xmp_content =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "GCamera:MicroVideo='1' GCamera:MicroVideoVersion='1' "
      "GCamera:MicroVideoPresentationTimestampUs='1500000'/></rdf:RDF></"
      "x:xmpmeta>";

  std::string jpeg_data;
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD8));  // SOI
  // APP1 XMP
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xE1));
  uint16_t app1_len = 2 + 29 + xmp_content.size();
  jpeg_data.push_back(static_cast<char>(app1_len >> 8));
  jpeg_data.push_back(static_cast<char>(app1_len & 0xFF));
  jpeg_data.append("http://ns.adobe.com/xap/1.0/\0", 29);
  jpeg_data.append(xmp_content);
  // EOI
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD9));
  // Trailer MP4 data
  std::string mp4_trailer = "....ftypmp42....";
  jpeg_data.append(mp4_trailer);

  image_io::StringRefDataSource data_source(jpeg_data);
  MetadataCollection collection =
      engine.Parse(&data_source, jpeg_data.size(), FileType::kJpeg);

  EXPECT_TRUE(MetadataEngine::IsMotionPhoto(collection));
  EXPECT_EQ(collection.blocks_size(),
            2);  // XMP block and container.trailer block
  EXPECT_EQ(collection.blocks(0).format_identifier(), "standard.xmp");
  EXPECT_EQ(collection.blocks(1).format_identifier(), "container.trailer");
}

TEST(LegacyMicroVideoProviderTest, GlobalRegistryIntegration) {
  MetadataProviderRegistry::Register3pProvider(
      std::make_shared<LegacyMicroVideoProvider>());

  image_io::MessageHandler message_handler;
  MetadataEngine engine(&message_handler);
  // Do not manually call engine.RegisterProvider; engine automatically polls
  // MetadataProviderRegistry::Get3pProviders().

  std::string xmp_content =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "GCamera:MicroVideo='1' GCamera:MicroVideoVersion='1' "
      "GCamera:MicroVideoPresentationTimestampUs='1500000'/></rdf:RDF></"
      "x:xmpmeta>";

  std::string jpeg_data;
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD8));  // SOI
  // APP1 XMP
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xE1));
  uint16_t app1_len = 2 + 29 + xmp_content.size();
  jpeg_data.push_back(static_cast<char>(app1_len >> 8));
  jpeg_data.push_back(static_cast<char>(app1_len & 0xFF));
  jpeg_data.append("http://ns.adobe.com/xap/1.0/\0", 29);
  jpeg_data.append(xmp_content);
  // EOI
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD9));
  // Trailer MP4 data
  std::string mp4_trailer = "....ftypmp42....";
  jpeg_data.append(mp4_trailer);

  image_io::StringRefDataSource data_source(jpeg_data);
  MetadataCollection collection =
      engine.Parse(&data_source, jpeg_data.size(), FileType::kJpeg);

  EXPECT_TRUE(MetadataEngine::IsMotionPhoto(collection));
  EXPECT_EQ(collection.blocks_size(), 2);
}

TEST(LegacyMicroVideoProviderTest, ExtractMetadata_LegacyMicroVideo) {
  MetadataProviderRegistry::Register3pProvider(
      std::make_shared<LegacyMicroVideoProvider>());

  image_io::MessageHandler message_handler;
  MetadataEngine engine(&message_handler);

  std::string xmp_content =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "GCamera:MicroVideo='1' GCamera:MicroVideoVersion='1' "
      "GCamera:MicroVideoPresentationTimestampUs='1500000'/></rdf:RDF></"
      "x:xmpmeta>";

  std::string jpeg_data;
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD8));  // SOI
  // APP1 XMP
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xE1));
  uint16_t app1_len = 2 + 29 + xmp_content.size();
  jpeg_data.push_back(static_cast<char>(app1_len >> 8));
  jpeg_data.push_back(static_cast<char>(app1_len & 0xFF));
  jpeg_data.append("http://ns.adobe.com/xap/1.0/\0", 29);
  jpeg_data.append(xmp_content);
  // EOI
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD9));
  // Trailer MP4 data
  std::string mp4_trailer = "....ftypmp42....";
  jpeg_data.append(mp4_trailer);

  std::string temp_input = "/tmp/test_legacy_micro_video_input.jpg";
  std::string temp_output = "/tmp/test_legacy_micro_video_extracted_meta.xml";

  std::ofstream ofs(temp_input, std::ios::binary);
  ofs.write(jpeg_data.data(), jpeg_data.size());
  ofs.close();

  EXPECT_TRUE(engine.ExtractMetadata(temp_input, temp_output));

  std::ifstream ifs(temp_output, std::ios::binary);
  std::string extracted_str((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
  ifs.close();
  EXPECT_EQ(extracted_str, xmp_content);

  // Test with disable_3p_plugins = true
  HandlerOptions disabled_options;
  disabled_options.disable_3p_plugins = true;
  std::string temp_disabled_output =
      "/tmp/test_legacy_micro_video_disabled.xml";
  EXPECT_FALSE(engine.ExtractMetadata(temp_input, temp_disabled_output,
                                      disabled_options));

  std::remove(temp_input.c_str());
  std::remove(temp_output.c_str());
  std::remove(temp_disabled_output.c_str());
}

TEST(LegacyMicroVideoProviderTest, ExtractMetadata_StandardGoogleMotionPhoto) {
  image_io::MessageHandler message_handler;
  MetadataEngine engine(&message_handler);
  engine.RegisterProvider(std::make_unique<GoogleMotionPhotoProvider>());

  std::string xmp_content =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "xmlns:Container='http://ns.google.com/photos/1.0/container/' "
      "GCamera:MotionPhoto='1' GCamera:MotionPhotoVersion='1' "
      "GCamera:MotionPhotoPresentationTimestampUs='500000'/></rdf:RDF></"
      "x:xmpmeta>";

  std::string jpeg_data;
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD8));  // SOI
  // APP1 XMP
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xE1));
  uint16_t app1_len = 2 + 29 + xmp_content.size();
  jpeg_data.push_back(static_cast<char>(app1_len >> 8));
  jpeg_data.push_back(static_cast<char>(app1_len & 0xFF));
  jpeg_data.append("http://ns.adobe.com/xap/1.0/\0", 29);
  jpeg_data.append(xmp_content);
  // EOI
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD9));
  // Trailer MP4 data
  std::string mp4_trailer = "....ftypmp42....";
  jpeg_data.append(mp4_trailer);

  std::string temp_input = "/tmp/test_standard_google_mp_input.jpg";
  std::string temp_output = "/tmp/test_standard_google_mp_extracted_meta.xml";

  std::ofstream ofs(temp_input, std::ios::binary);
  ofs.write(jpeg_data.data(), jpeg_data.size());
  ofs.close();

  EXPECT_TRUE(engine.ExtractMetadata(temp_input, temp_output));

  std::ifstream ifs(temp_output, std::ios::binary);
  std::string extracted_str((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
  ifs.close();
  EXPECT_EQ(extracted_str, xmp_content);

  std::remove(temp_input.c_str());
  std::remove(temp_output.c_str());
}

TEST(LegacyMicroVideoProviderTest,
     ExtractMetadata_NonMotionPhoto_ReturnsFalse) {
  image_io::MessageHandler message_handler;
  MetadataEngine engine(&message_handler);
  engine.RegisterProvider(std::make_unique<GoogleMotionPhotoProvider>());

  std::string container_only_content =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description "
      "xmlns:Container='http://ns.google.com/photos/1.0/container/' "
      "Container:Version='1'/></rdf:RDF></x:xmpmeta>";

  std::string jpeg_data;
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD8));  // SOI
  // APP1 XMP
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xE1));
  uint16_t app1_len = 2 + 29 + container_only_content.size();
  jpeg_data.push_back(static_cast<char>(app1_len >> 8));
  jpeg_data.push_back(static_cast<char>(app1_len & 0xFF));
  jpeg_data.append("http://ns.adobe.com/xap/1.0/\0", 29);
  jpeg_data.append(container_only_content);
  // EOI
  jpeg_data.push_back(static_cast<char>(0xFF));
  jpeg_data.push_back(static_cast<char>(0xD9));

  std::string temp_input = "/tmp/test_non_mp_input.jpg";
  std::string temp_output = "/tmp/test_non_mp_extracted_meta.xml";

  std::ofstream ofs(temp_input, std::ios::binary);
  ofs.write(jpeg_data.data(), jpeg_data.size());
  ofs.close();

  EXPECT_FALSE(engine.ExtractMetadata(temp_input, temp_output));

  std::remove(temp_input.c_str());
  std::remove(temp_output.c_str());
}

}  // namespace motion_photo
}  // namespace libmotionphoto
