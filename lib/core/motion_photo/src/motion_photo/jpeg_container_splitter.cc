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
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"
#include "image_io/jpeg/jpeg_marker.h"
#include "image_io/jpeg/jpeg_scanner.h"
#include "image_io/jpeg/jpeg_segment_processor.h"
#include "motion_photo/container_splitter.h"

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
  JpegContainerSplitter(image_io::MessageHandler* message_handler)
      : message_handler_(message_handler) {}

  std::vector<RawMetadataBlock> Split(image_io::DataSource* data_source,
                                      size_t file_size) override {
    blocks_.clear();
    eoi_location_ = 0;

    image_io::JpegScanner scanner(message_handler_);

    image_io::JpegMarker::Flags flags;
    flags.set(image_io::JpegMarker::kAPP0);
    flags.set(image_io::JpegMarker::kAPP1);
    flags.set(image_io::JpegMarker::kAPP2);
    flags.set(image_io::JpegMarker::kEOI);

    scanner.UpdateInterestingMarkerFlags(flags);
    scanner.Run(data_source, this);

    if (eoi_location_ > 0 && eoi_location_ < file_size) {
      size_t start = eoi_location_ + 2;  // skip EOI marker (0xFFD9)
      if (start < file_size) {
        RawMetadataBlock block;
        block.type = "PROPRIETARY";
        block.format_identifier = "container.trailer";
        block.offset = start;
        if (CopyFromDataSource(data_source, start, file_size - start,
                               &block.bytes)) {
          blocks_.push_back(block);
        }
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
      }
    } else if (marker.GetType() == image_io::JpegMarker::kAPP2) {
      if (segment.BytesAtLocationStartWith(segment.GetPayloadDataLocation(),
                                           "ICC_PROFILE\0")) {
        ExtractBlock(segment, "ICC", "standard.icc", 14);  // skip signature and chunk info
      }
    } else if (marker.GetType() == image_io::JpegMarker::kEOI) {
      eoi_location_ = segment.GetBegin();
      scanner->SetDone();
    }
  }

  void Finish(image_io::JpegScanner* scanner) override {}

 private:
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
