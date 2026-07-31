# External Contribution Guide: Supporting Legacy Micro Video Motion Photo Format

**Tl;Dr** This document provides detailed guidance on integrating and supporting
the legacy Micro Video format (e.g., Pixel 1 & Pixel 2 `GCamera:MicroVideo`
format) into `libmotionphoto` using the **Motion Photo Provider Interface
(MPPI)**.

# Overview

The `libmotionphoto` Metadata Engine uses a modular **Broker-Producer** model.
The core framework (Broker) handles high-level container parsing (splitting) and
orchestrates the workflow.

Format-specific parsing of proprietary and legacy metadata is delegated to
**Plugins (Producers)** that implement the `IMetadataProvider` interface. These
plugins run inside a sandboxed environment for security.

### When do you need a plugin?

If an image writes motion photos where the video or metadata is stored in a
non-standard or legacy container format (e.g., Pixel 1/2 Micro Video using
`GCamera:MicroVideo` XMP attributes and an appended MP4 trailer after JPEG EOF)
rather than standard ISO/XMP container directory specs, you must provide a
plugin to:

1.  **Identify** the format's signature (e.g. `MicroVideo` in XMP and MP4
    container trailer).
2.  **Expose video location** (offset and length) to the framework.
3.  **Parse raw bytes into a structured semantic model** (Protobuf).

# Integration Steps

Integrating support for a legacy or custom format involves four steps:

1.  **Define the Semantic Proto**: Define the structured data model for the
    legacy metadata.
2.  **Implement the C++ Plugin**: Implement the `IMetadataProvider` interface.
3.  **Register the Plugin**: Register the plugin with the global
    `MetadataProviderRegistry`.
4.  **Write Unit Tests**: Verify identification, video offset calculations, and
    semantic decoding against real and synthetic image assets.

# Step-by-Step Implementation Guide

We isolate legacy micro video handling under `lib/core/plugins/legacy_micro_video/`.

## Step 1: Define the Semantic Proto

Create a Protobuf file defining the schema of the decoded legacy metadata. This
proto will be serialized into `google.protobuf.Any` payload inside
`MetadataBlock.decoded_payload`.

File: `lib/core/plugins/legacy_micro_video/legacy_micro_video_metadata.proto`

```protobuf
syntax = "proto3";

package libmotionphoto.motion_photo;

option java_multiple_files = true;
option java_package = "com.google.libmotionphoto.motionphoto.proto";
option optimize_for = LITE_RUNTIME;

message LegacyMicroVideoMetadata {
  int32 legacy_micro_video = 1;
  int32 version = 2;
  int64 primary_image_timestamp_us = 3;
}
```

## Step 2: Implement the `IMetadataProvider` Interface

Your plugin inherits from `IMetadataProvider` (defined in
`motion_photo/metadata_provider.h`).

### 1. Fast-Path Identification: `Identify()`

The framework queries `Identify()` on registered plugins to test if any plugin
recognizes the raw metadata block.

*   **Input**: `RawMetadataBlock` (contains block type, format identifier, and
    raw bytes).
*   **Output**: Return `true` if the block belongs to your format; `false`
    otherwise.
*   **Constraint**: Keep this fast. Match signatures (e.g., checking for
    `MicroVideo` in XMP or MP4 atom tags
    `ftyp`/`moov`/`mdat`/`free`/`skip`/`wide` in trailer blocks).

### 2. Specifying Video Location: `GetVideoInfo()`

When a proprietary block contains the video file payload (such as an appended
MP4 trailer), the framework queries video location and length to extract the
stream.

*   **Input**: `RawMetadataBlock`.
*   **Output**: Populate `out_offset_in_block` (relative to the start of this
    block) and `out_length` (in bytes). Return `true` if video was located;
    `false` otherwise.

### 3. Deep Parsing: `DecodeToSemantic()`

If `Identify()` returned `true`, the framework invokes `DecodeToSemantic()`
inside the sandbox to decode raw bytes into the structured proto.

*   **Input**: `RawMetadataBlock`.
*   **Output**:
    *   `out_proto_bytes`: Serialized byte stream of `LegacyMicroVideoMetadata`.
    *   `out_type_url`: Type URL
        (`type.googleapis.com/libmotionphoto.motion_photo.LegacyMicroVideoMetadata`).
    *   `error_message`: Populated with failure details if parsing fails.
    *   Return `true` on success; `false` on failure.

### 4. Plugin Name & Type Check: `GetPluginName()`, `IsMotionPhoto()`

*   `GetPluginName()`: Return `"legacy_micro_video"` (used for allowlisting and
    disabling plugins via `HandlerOptions`).
*   `IsMotionPhoto()`: Return `true` if the decoded `google.protobuf.Any`
    payload has `legacy_micro_video == 1`.

## Step 3: Registration

To ensure the framework discovers the plugin, register it within
`MetadataProviderRegistry` (`metadata_provider_registry.cc`).

## Step 4: Unit Test

Verify implementation by writing unit tests covering:

1.  `Identify()` matches valid legacy XMP & trailer blocks and rejects
    standard/foreign blocks.
2.  `DecodeToSemantic()` extracts timestamps, versions, and flags into
    `LegacyMicroVideoMetadata`.
3.  `GetVideoInfo()` returns correct offset and size.
4.  `MetadataEngine` end-to-end integration and `ExtractMetadata` behavior.

--------------------------------------------------------------------------------

# Reference Implementation

Below is the complete implementation for the `LegacyMicroVideoProvider` plugin
located in `lib/core/plugins/legacy_micro_video/`.

## Header File (`legacy_micro_video_provider.h`)

```cpp
#ifndef MOTION_PHOTO_PLUGINS_LEGACY_MICRO_VIDEO_LEGACY_MICRO_VIDEO_PROVIDER_H_
#define MOTION_PHOTO_PLUGINS_LEGACY_MICRO_VIDEO_LEGACY_MICRO_VIDEO_PROVIDER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_provider.h"

namespace libmotionphoto {
namespace motion_photo {

/// A 3P metadata provider plugin that recognizes and decodes legacy Micro Video
/// (GCamera / Camera MicroVideo XMP metadata and concatenated MP4 trailer).
class LegacyMicroVideoProvider : public IMetadataProvider {
 public:
  LegacyMicroVideoProvider() = default;
  ~LegacyMicroVideoProvider() override = default;

  bool Identify(const RawMetadataBlock& block) override;

  bool DecodeToSemantic(const RawMetadataBlock& block,
                        std::vector<uint8_t>* out_proto_bytes,
                        std::string* out_type_url,
                        std::string* error_message) override;

  std::string GetPluginName() const override { return "legacy_micro_video"; }

  bool IsMotionPhoto(const std::string& type_url,
                     const std::string& payload_bytes) const override;

  bool GetVideoInfo(const RawMetadataBlock& block, size_t* out_offset_in_block,
                    size_t* out_length) const override;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_PLUGINS_LEGACY_MICRO_VIDEO_LEGACY_MICRO_VIDEO_PROVIDER_H_
```

## Source File (`legacy_micro_video_provider.cc`)

```cpp
#include "plugins/legacy_micro_video/legacy_micro_video_provider.h"

#include <absl/strings/match.h>

#include <cstddef>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "legacy_micro_video_metadata.pb.h"
#include "motion_photo/metadata_block.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

int64_t ExtractInt64Attr(std::string_view xml, const std::string& attr_name) {
  std::regex attr_regex(R"((?:GCamera|Camera):)" + attr_name +
                        R"(\s*=\s*["'](-?\d+)["'])");
  std::cmatch match;
  if (std::regex_search(xml.data(), xml.data() + xml.size(), match,
                        attr_regex)) {
    return std::stoll(match[1].str());
  }
  std::regex elem_regex(R"(<(?:GCamera|Camera):)" + attr_name +
                        R"(>[\s]*(-?\d+)[\s]*</(?:GCamera|Camera):)" +
                        attr_name + R"(>)");
  if (std::regex_search(xml.data(), xml.data() + xml.size(), match,
                        elem_regex)) {
    return std::stoll(match[1].str());
  }
  return -1;
}

}  // namespace

bool LegacyMicroVideoProvider::Identify(const RawMetadataBlock& block) {
  if (block.type == "XMP" && block.format_identifier == "standard.xmp") {
    std::string_view xml(reinterpret_cast<const char*>(block.bytes.data()),
                         block.bytes.size());
    return absl::StrContains(xml, "MicroVideo");
  }
  if (block.format_identifier == "container.trailer") {
    if (block.bytes.size() >= 8) {
      std::string_view tag(
          reinterpret_cast<const char*>(block.bytes.data() + 4), 4);
      if (tag == "ftyp" || tag == "moov" || tag == "mdat" || tag == "free" ||
          tag == "skip" || tag == "wide") {
        return true;
      }
    }
  }
  return false;
}

bool LegacyMicroVideoProvider::DecodeToSemantic(
    const RawMetadataBlock& block, std::vector<uint8_t>* out_proto_bytes,
    std::string* out_type_url, std::string* error_message) {
  if (block.type != "XMP") {
    return false;
  }
  std::string_view xml(reinterpret_cast<const char*>(block.bytes.data()),
                       block.bytes.size());

  LegacyMicroVideoMetadata metadata;
  int64_t micro_video = ExtractInt64Attr(xml, "MicroVideo");
  if (micro_video > 0) {
    metadata.set_legacy_micro_video(micro_video);
  }

  int64_t timestamp =
      ExtractInt64Attr(xml, "MicroVideoPresentationTimestampUs");
  if (timestamp >= 0) {
    metadata.set_primary_image_timestamp_us(timestamp);
  }

  int64_t version = ExtractInt64Attr(xml, "MicroVideoVersion");
  if (version > 0) {
    metadata.set_version(version);
  }

  if (out_type_url) {
    *out_type_url =
        "type.googleapis.com/libmotionphoto.motion_photo."
        "LegacyMicroVideoMetadata";
  }

  out_proto_bytes->resize(metadata.ByteSizeLong());
  if (!metadata.SerializeToArray(out_proto_bytes->data(),
                                 out_proto_bytes->size())) {
    if (error_message) {
      *error_message = "Failed to serialize LegacyMicroVideoMetadata proto";
    }
    return false;
  }

  return true;
}

bool LegacyMicroVideoProvider::IsMotionPhoto(
    const std::string& type_url, const std::string& payload_bytes) const {
  if (!absl::StrContains(type_url, "LegacyMicroVideoMetadata")) {
    return false;
  }
  LegacyMicroVideoMetadata metadata;
  if (!metadata.ParseFromString(payload_bytes)) {
    return false;
  }
  return metadata.legacy_micro_video() == 1;
}

bool LegacyMicroVideoProvider::GetVideoInfo(const RawMetadataBlock& block,
                                            size_t* out_offset_in_block,
                                            size_t* out_length) const {
  if (block.format_identifier == "container.trailer") {
    if (out_offset_in_block) {
      *out_offset_in_block = 0;
    }
    if (out_length) {
      *out_length = block.bytes.size();
    }
    return true;
  }
  return false;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
```

## Registration (`metadata_provider_registry.cc`)

Register the plugin statically inside `MetadataProviderRegistry`:

```cpp
#include "motion_photo/metadata_provider_registry.h"

#include <memory>
#include <utility>
#include <vector>

#include "motion_photo/metadata_provider.h"
#include "plugins/legacy_micro_video/legacy_micro_video_provider.h"

namespace libmotionphoto {
namespace motion_photo {

// static
void MetadataProviderRegistry::Register3pProvider(
    std::shared_ptr<IMetadataProvider> provider) {
  Registry().push_back(std::move(provider));
}

// static
std::vector<std::shared_ptr<IMetadataProvider>>
MetadataProviderRegistry::Get3pProviders() {
  return Registry();
}

// static
std::vector<std::shared_ptr<IMetadataProvider>>&
MetadataProviderRegistry::Registry() {
  static auto* registry = new std::vector<std::shared_ptr<IMetadataProvider>>{
      std::make_shared<LegacyMicroVideoProvider>(),
  };
  return *registry;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
```

## Testing Your Plugin (`legacy_micro_video_provider_test.cc`)

```cpp
#include "plugins/legacy_micro_video/legacy_micro_video_provider.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "legacy_micro_video_metadata.pb.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/tests/test_framework.h"

namespace libmotionphoto {
namespace motion_photo {
namespace {

TEST(LegacyMicroVideoProviderTest, IdentifyValidXmp) {
  LegacyMicroVideoProvider provider;
  RawMetadataBlock block;
  block.type = "XMP";
  block.format_identifier = "standard.xmp";
  std::string xmp_data =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "GCamera:MicroVideo='1' GCamera:MicroVideoVersion='1' "
      "GCamera:MicroVideoOffset='4261545'/></rdf:RDF></x:xmpmeta>";
  block.bytes.assign(xmp_data.begin(), xmp_data.end());

  EXPECT_TRUE(provider.Identify(block));
}

TEST(LegacyMicroVideoProviderTest, DecodeToSemanticSucceeds) {
  LegacyMicroVideoProvider provider;
  RawMetadataBlock block;
  block.type = "XMP";
  block.format_identifier = "standard.xmp";
  std::string xmp_data =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:"
      "Description xmlns:GCamera='http://ns.google.com/photos/1.0/camera/' "
      "GCamera:MicroVideo='1' GCamera:MicroVideoVersion='1' "
      "GCamera:MicroVideoPresentationTimestampUs='1475413'/></rdf:RDF></x:xmpmeta>";
  block.bytes.assign(xmp_data.begin(), xmp_data.end());

  std::vector<uint8_t> proto_bytes;
  std::string type_url;
  std::string error_message;

  EXPECT_TRUE(provider.DecodeToSemantic(block, &proto_bytes, &type_url, &error_message));
  EXPECT_EQ(type_url, "type.googleapis.com/libmotionphoto.motion_photo.LegacyMicroVideoMetadata");

  LegacyMicroVideoMetadata metadata;
  std::string payload_str(reinterpret_cast<char*>(proto_bytes.data()), proto_bytes.size());
  EXPECT_TRUE(metadata.ParseFromString(payload_str));
  EXPECT_EQ(metadata.legacy_micro_video(), 1);
  EXPECT_EQ(metadata.primary_image_timestamp_us(), 1475413);
  EXPECT_EQ(metadata.version(), 1);
  EXPECT_TRUE(provider.IsMotionPhoto(type_url, payload_str));
}

TEST(LegacyMicroVideoProviderTest, GetVideoInfoSucceeds) {
  LegacyMicroVideoProvider provider;
  RawMetadataBlock trailer_block;
  trailer_block.type = "PROPRIETARY";
  trailer_block.format_identifier = "container.trailer";
  trailer_block.bytes.resize(1024);

  size_t offset = 0;
  size_t length = 0;
  EXPECT_TRUE(provider.GetVideoInfo(trailer_block, &offset, &length));
  EXPECT_EQ(offset, 0);
  EXPECT_EQ(length, 1024);
}

}  // namespace
}  // namespace motion_photo
}  // namespace libmotionphoto
```
