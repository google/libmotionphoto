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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"
#include "image_io/jpeg/jpeg_marker.h"
#include "image_io/jpeg/jpeg_scanner.h"
#include "image_io/jpeg/jpeg_segment.h"
#include "image_io/jpeg/jpeg_segment_processor.h"
#include "motion_photo/container_splitter.h"
#include "motion_photo/metadata_block.h"
#include "motion_photo/motion_photo.h"

namespace libmotionphoto {
namespace motion_photo {

namespace {

bool CopyFromDataSource(image_io::DataSource* source, size_t start,
                        size_t length, std::vector<uint8_t>* out) {
  out->resize(length);
  size_t bytes_copied = 0;
  while (bytes_copied < length) {
    size_t chunk_start = start + bytes_copied;
    size_t chunk_size = length - bytes_copied;
    auto segment = source->GetDataSegment(chunk_start, chunk_size);
    if (!segment) {
      return false;
    }
    const uint8_t* buf = segment->GetBuffer(chunk_start);
    if (!buf) {
      return false;
    }
    size_t copy_limit =
        std::min(chunk_size, segment->GetDataRange().GetEnd() - chunk_start);
    std::copy(buf, buf + copy_limit, out->begin() + bytes_copied);
    bytes_copied += copy_limit;
  }
  return true;
}

class JpegContainerSplitter : public ContainerSplitter,
                              public image_io::JpegSegmentProcessor {
 public:
  explicit JpegContainerSplitter(image_io::MessageHandler* message_handler)
      : message_handler_(message_handler) {}

  std::vector<RawMetadataBlock> Split(image_io::DataSource* data_source,
                                      size_t file_size) override {
    blocks_.clear();
    eoi_location_ = 0;
    xmp_video_length_ = 0;

    image_io::JpegScanner scanner(message_handler_);

    image_io::JpegMarker::Flags flags;
    flags.set(image_io::JpegMarker::kAPP0);
    flags.set(image_io::JpegMarker::kAPP1);
    flags.set(image_io::JpegMarker::kAPP2);
    flags.set(image_io::JpegMarker::kSOS);
    flags.set(image_io::JpegMarker::kEOI);

    scanner.UpdateInterestingMarkerFlags(flags);
    scanner.Run(data_source, this);

    size_t trailer_start = 0;
    size_t trailer_length = 0;

    if (xmp_video_length_ > 0 && xmp_video_length_ < file_size) {
      trailer_start = file_size - xmp_video_length_;
      trailer_length = xmp_video_length_;
    } else if (eoi_location_ > 0 && eoi_location_ < file_size) {
      trailer_start = eoi_location_ + 2;  // skip EOI marker (0xFFD9)
      if (trailer_start < file_size) {
        trailer_length = file_size - trailer_start;
      }
    }

    if (trailer_start > 0 && trailer_start < file_size && trailer_length > 0) {
      RawMetadataBlock block;
      block.type = "PROPRIETARY";
      block.format_identifier = "container.trailer";
      block.offset = trailer_start;
      block.total_payload_size = trailer_length;
      // Copy only header bytes needed for format verification (e.g. ftyp, moov)
      // to avoid allocating/copying large video payloads (up to 120 MB) into
      // RAM.
      size_t bytes_to_copy = std::min<size_t>(trailer_length, 64);
      if (CopyFromDataSource(data_source, trailer_start, bytes_to_copy,
                             &block.bytes)) {
        blocks_.push_back(std::move(block));
      }
    }
    return blocks_;
  }

  void Start(image_io::JpegScanner* scanner) override {}

  void Process(image_io::JpegScanner* scanner,
               const image_io::JpegSegment& segment) override {
    image_io::JpegMarker marker = segment.GetMarker();
    if (marker.GetType() == image_io::JpegMarker::kAPP0) {
      if (segment.BytesAtLocationStartWith(segment.GetPayloadDataLocation(),
                                           "JFIF\0")) {
        ExtractBlock(segment, "JFIF", "standard.jfif", 5);  // skip "JFIF\0"
      }
    } else if (marker.GetType() == image_io::JpegMarker::kAPP1) {
      if (segment.BytesAtLocationStartWith(segment.GetPayloadDataLocation(),
                                           "Exif\0\0")) {
        ExtractBlock(segment, "EXIF", "standard.exif", 6);  // skip "Exif\0\0"
      } else if (segment.BytesAtLocationStartWith(
                     segment.GetPayloadDataLocation(),
                     "http://ns.adobe.com/xap/1.0/\0")) {
        ExtractBlock(segment, "XMP", "standard.xmp", 29);  // skip namespace
        if (!blocks_.empty() &&
            blocks_.back().format_identifier == "standard.xmp") {
          std::string_view xml(
              reinterpret_cast<const char*>(blocks_.back().bytes.data()),
              blocks_.back().bytes.size());
          size_t length = ParseVideoLengthFromXmp(xml);
          if (length > 0) {
            xmp_video_length_ = length;
          }
        }
      }
    } else if (marker.GetType() == image_io::JpegMarker::kAPP2) {
      if (segment.BytesAtLocationStartWith(segment.GetPayloadDataLocation(),
                                           "ICC_PROFILE\0")) {
        ExtractBlock(segment, "ICC", "standard.icc", 14);  // skip signature and chunk info
      }
    } else if (marker.GetType() == image_io::JpegMarker::kSOS) {
      if (xmp_video_length_ > 0) {
        // All metadata segments precede SOS. If video trailer bounds are
        // already known from XMP, stop scanning entropy-coded image data.
        scanner->SetDone();
      }
    } else if (marker.GetType() == image_io::JpegMarker::kEOI) {
      eoi_location_ = segment.GetBegin();
      scanner->SetDone();
    }
  }

  void Finish(image_io::JpegScanner* scanner) override {}

 private:
  static size_t ParseVideoLengthFromXmp(std::string_view xml) {
    // 1. Check Container:Directory Item:Length where
    // Item:Semantic="MotionPhoto" Handles both Item:Semantic="MotionPhoto"
    // Item:Length="..." and reversed attribute order.
    size_t motion_photo_pos = xml.find("MotionPhoto");
    while (motion_photo_pos != std::string_view::npos) {
      // Find enclosing tag bounds '<...>'
      size_t tag_start = xml.rfind('<', motion_photo_pos);
      size_t tag_end = xml.find('>', motion_photo_pos);
      if (tag_start != std::string_view::npos &&
          tag_end != std::string_view::npos && tag_start < tag_end) {
        std::string_view tag = xml.substr(tag_start, tag_end - tag_start + 1);
        absl::string_view absl_tag(tag.data(), tag.size());
        if (absl::StrContains(absl_tag, "Item:Length") ||
            absl::StrContains(absl_tag, "Length")) {
          size_t len_attr = tag.find("Length=\"");
          size_t val_start = (len_attr != std::string_view::npos)
                                 ? len_attr + 8
                                 : std::string_view::npos;
          if (val_start == std::string_view::npos) {
            len_attr = tag.find("Length='");
            if (len_attr != std::string_view::npos) {
              val_start = len_attr + 8;
            }
          }
          if (val_start != std::string_view::npos) {
            char quote = tag[val_start - 1];
            size_t val_end = tag.find(quote, val_start);
            if (val_end != std::string_view::npos) {
              std::string val_str(tag.substr(val_start, val_end - val_start));
              try {
                int64_t len = std::stoll(val_str);
                if (len > 0) return static_cast<size_t>(len);
              } catch (...) {
              }
            }
          }
        }
      }
      motion_photo_pos = xml.find("MotionPhoto", motion_photo_pos + 1);
    }

    // 2. Check GCamera:MicroVideoOffset / Camera:MicroVideoOffset
    size_t offset_pos = xml.find("MicroVideoOffset=\"");
    size_t val_start = (offset_pos != std::string_view::npos)
                           ? offset_pos + 18
                           : std::string_view::npos;
    if (val_start == std::string_view::npos) {
      offset_pos = xml.find("MicroVideoOffset='");
      if (offset_pos != std::string_view::npos) {
        val_start = offset_pos + 18;
      }
    }
    if (val_start != std::string_view::npos) {
      char quote = xml[val_start - 1];
      size_t val_end = xml.find(quote, val_start);
      if (val_end != std::string_view::npos) {
        std::string val_str(xml.substr(val_start, val_end - val_start));
        try {
          int64_t len = std::stoll(val_str);
          if (len > 0) return static_cast<size_t>(len);
        } catch (...) {
        }
      }
    }

    // Check XML element: <(GCamera|Camera):MicroVideoOffset>...</...>
    size_t elem_pos = xml.find(":MicroVideoOffset>");
    if (elem_pos != std::string_view::npos) {
      size_t text_start = elem_pos + 18;
      size_t text_end = xml.find('<', text_start);
      if (text_end != std::string_view::npos) {
        std::string val_str(xml.substr(text_start, text_end - text_start));
        try {
          int64_t len = std::stoll(val_str);
          if (len > 0) return static_cast<size_t>(len);
        } catch (...) {
        }
      }
    }

    return 0;
  }

  void ExtractBlock(const image_io::JpegSegment& segment, std::string_view type,
                    std::string_view format_id, size_t skip_bytes) {
    size_t start = segment.GetPayloadDataLocation() + skip_bytes;
    size_t end = segment.GetDataRange().GetEnd();
    if (start < end) {
      RawMetadataBlock block;
      block.type = std::string(type);
      block.format_identifier = std::string(format_id);
      block.offset = start;
      block.bytes.resize(end - start);
      for (size_t loc = start; loc < end; ++loc) {
        block.bytes[loc - start] = segment.GetValidatedByte(loc).value;
      }
      blocks_.push_back(std::move(block));
    }
  }

  image_io::MessageHandler* message_handler_;
  std::vector<RawMetadataBlock> blocks_;
  size_t eoi_location_ = 0;
  size_t xmp_video_length_ = 0;
};

}  // namespace

std::unique_ptr<ContainerSplitter> CreateContainerSplitter(
    FileType type, image_io::MessageHandler* message_handler) {
  if (type == FileType::kJpeg) {
    return std::make_unique<JpegContainerSplitter>(message_handler);
  }
  return nullptr;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
