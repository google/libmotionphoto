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

#include "motion_photo_jpeg/motion_photo_jpeg_image_builder.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include "image_io/base/byte_buffer.h"
#include "image_io/base/byte_data.h"
#include "image_io/base/data_range.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/ostream_data_destination.h"
#include "image_io/jpeg/jpeg_marker.h"
#include "image_io/jpeg/jpeg_segment_builder.h"
#include "image_io/jpeg/jpeg_segment_info.h"
#include "image_io/mpf/mpf_info.h"
#include "image_io/mpf/mpf_info_constants.h"
#include "image_io/mpf/mpf_info_decoder.h"
#include "image_io/mpf/mpf_info_encoder.h"
#include "image_io/utils/file_utils.h"
#include "motion_photo/motion_photo_source.h"
#include "motion_photo_jpeg/motion_photo_jpeg_info_builder.h"
#include "absl/algorithm/container.h"

namespace libmotionphoto {
namespace motion_photo {

using libmotionphoto::image_io::ByteBuffer;
using libmotionphoto::image_io::ByteData;
using libmotionphoto::image_io::DataRange;
using libmotionphoto::image_io::DataSegment;
using libmotionphoto::image_io::DataSegmentDataSource;
using libmotionphoto::image_io::DataSource;
using libmotionphoto::image_io::JpegMarker;
using libmotionphoto::image_io::JpegSegmentBuilder;
using libmotionphoto::image_io::kExif;
using libmotionphoto::image_io::kJfif;
using libmotionphoto::image_io::kXmpId;
using libmotionphoto::image_io::Message;
using libmotionphoto::image_io::MessageHandler;
using libmotionphoto::image_io::Byte;
using libmotionphoto::image_io::JpegSegmentInfo;
using libmotionphoto::image_io::kMpf;
using libmotionphoto::image_io::MpfInfo;
using libmotionphoto::image_io::MpfInfoDecoder;
using libmotionphoto::image_io::MpfInfoEncoder;
using std::shared_ptr;
using std::string;
using std::vector;

namespace {

bool ExtractRangesFromInfo(const MotionPhotoJpegInfoBuilder& info_builder,
                           MessageHandler* message_handler,
                           DataRange* image_range, DataRange* cut_range,
                           DataRange* mpf_range,
                           size_t* old_primary_image_size) {
  const auto& jpeg_info = info_builder.GetInfo();
  if (jpeg_info.GetImageRanges().empty()) {
    message_handler->ReportMessage(Message::kValueError,
                                   "File does not contain any images");
    return false;
  }
  *cut_range = info_builder.GetPrimaryXmpSegmentRange();
  DataRange jfif_range = jpeg_info.GetSegmentInfo(0, kJfif).GetDataRange();
  DataRange exif_range = jpeg_info.GetSegmentInfo(0, kExif).GetDataRange();
  if (!cut_range->IsValid() && !jfif_range.IsValid() && !exif_range.IsValid()) {
    message_handler->ReportMessage(
        Message::kValueError,
        "Primary image of file is missing the segments needed for adding\n"
        "- a modified XMP segment: a JFIF, EXIF or XMP segment");
    return false;
  }
  if (!cut_range->IsValid()) {
    size_t end = jfif_range.IsValid() ? jfif_range.GetEnd() : 0;
    end = exif_range.IsValid() ? std::max(end, exif_range.GetEnd()) : end;
    *cut_range = DataRange(end, end);
  }
  *image_range = DataRange(0, jpeg_info.GetImageRanges().back().GetEnd());

  JpegSegmentInfo mpf_segment_info = jpeg_info.GetSegmentInfo(0, kMpf);
  if (mpf_segment_info.IsValid()) {
    *mpf_range = mpf_segment_info.GetDataRange();
  } else {
    *mpf_range = DataRange();
  }

  *old_primary_image_size = jpeg_info.GetImageRanges().front().GetLength();
  return true;
}

bool GetRanges(DataSource* data_source, MessageHandler* message_handler,
               DataRange* image_range, DataRange* cut_range,
               DataRange* mpf_range, size_t* old_primary_image_size) {
  MotionPhotoJpegInfoBuilder info_builder(message_handler);
  info_builder.Build(data_source);
  return ExtractRangesFromInfo(info_builder, message_handler, image_range,
                               cut_range, mpf_range, old_primary_image_size);
}

shared_ptr<DataSegment> CreateXmpDataSegment(std::string_view xmp_metadata) {
  JpegSegmentBuilder builder;
  builder.AddMarkerAndSizePlaceholder(JpegMarker::kAPP1);
  builder.AddByteData(ByteData(ByteData::kAscii0, kXmpId));
  builder.AddByteData(ByteData(ByteData::kAscii, std::string(xmp_metadata).c_str()));
  ByteBuffer byte_buffer(builder.GetByteData());
  if (!JpegSegmentBuilder::SetPayloadSize(&byte_buffer)) {
    return nullptr;
  }
  DataRange data_range(0, byte_buffer.GetSize());
  return DataSegment::Create(data_range, byte_buffer.Release());
}

}  // namespace

MotionPhotoJpegImageBuilder::MotionPhotoJpegImageBuilder(
    const string& output_file_name, image_io::MessageHandler* message_handler)
    : message_handler_(message_handler), output_file_name_(output_file_name) {}

MotionPhotoJpegImageBuilder::MotionPhotoJpegImageBuilder(
    image_io::MessageHandler* message_handler)
    : message_handler_(message_handler) {}

bool MotionPhotoJpegImageBuilder::Build(
    const MediaSource& image_source, std::string_view xmp_metadata,
    DataSink* output_sink, const MotionPhotoJpegInfoBuilder* prebuilt_info) {
  if (output_sink == nullptr || image_source.GetSize() == 0) {
    return false;
  }

  DataRange image_range, cut_range, mpf_range;
  size_t old_primary_image_size = 0;

  if (prebuilt_info != nullptr) {
    if (!ExtractRangesFromInfo(*prebuilt_info, message_handler_, &image_range,
                               &cut_range, &mpf_range,
                               &old_primary_image_size)) {
      return false;
    }
  } else {
    // Obtain a DataSegment representing the image data for structural parsing
    shared_ptr<DataSegment> image_segment;
    const uint8_t* direct_data = image_source.GetData();
    if (direct_data != nullptr) {
      DataRange range(0, image_source.GetSize());
      image_segment = DataSegment::Create(range, const_cast<Byte*>(direct_data),
                                          DataSegment::kDontDelete);
    } else {
      // Read image payload into segment for metadata parsing
      size_t size = image_source.GetSize();
      Byte* buffer = new Byte[size];
      if (!image_source.ReadAt(0, size, buffer)) {
        delete[] buffer;
        return false;
      }
      image_segment =
          DataSegment::Create(DataRange(0, size), buffer, DataSegment::kDelete);
    }

    DataSegmentDataSource ds_image_source(image_segment);
    if (!GetRanges(&ds_image_source, message_handler_, &image_range, &cut_range,
                   &mpf_range, &old_primary_image_size)) {
      return false;
    }
  }

  // Create new XMP data segment
  auto xmp_segment = CreateXmpDataSegment(xmp_metadata);
  if (!xmp_segment) {
    return false;
  }
  size_t new_xmp_size = xmp_segment->GetDataRange().GetLength();
  size_t old_xmp_size = cut_range.GetLength();
  ssize_t delta_xmp = new_xmp_size - old_xmp_size;

  // Handle MPF if present
  shared_ptr<DataSegment> new_mpf_segment = nullptr;
  ssize_t delta_mpf = 0;

  if (mpf_range.IsValid() && old_primary_image_size > 0) {
    vector<Byte> mpf_bytes_vec;
    const Byte* mpf_bytes = nullptr;
    const uint8_t* direct_data = image_source.GetData();
    if (direct_data != nullptr) {
      mpf_bytes = direct_data + mpf_range.GetBegin();
    } else {
      mpf_bytes_vec.resize(mpf_range.GetLength());
      if (image_source.ReadAt(mpf_range.GetBegin(), mpf_range.GetLength(),
                              mpf_bytes_vec.data())) {
        mpf_bytes = mpf_bytes_vec.data();
      }
    }

    if (mpf_bytes != nullptr) {
      MpfInfoDecoder decoder(message_handler_);
      size_t skip1 =
          decoder.SkipApp2Identifier(mpf_bytes, mpf_range.GetLength());
      size_t skip2 = decoder.SkipMpfIdentifier(mpf_bytes + skip1,
                                               mpf_range.GetLength() - skip1);
      size_t header_offset = skip1 + skip2;
      if (decoder.Decode(mpf_bytes + header_offset,
                         mpf_range.GetLength() - header_offset)) {
        MpfInfo mpf_info = decoder.GetInfo();
        if (!mpf_info.entries.empty()) {
          size_t old_mpf_size = mpf_range.GetLength();
          size_t new_mpf_size = MpfInfoEncoder::GetEncodedSize(mpf_info, true);
          delta_mpf = static_cast<ssize_t>(new_mpf_size) -
                      static_cast<ssize_t>(old_mpf_size);

          size_t new_primary_image_size =
              old_primary_image_size + delta_xmp + delta_mpf;
          mpf_info.entries[0].image_size = new_primary_image_size;

          MpfInfoEncoder encoder;
          const auto& encoded_mpf = encoder.Encode(mpf_info, true);

          Byte* mpf_buffer = new Byte[encoded_mpf.size()];
          absl::c_copy(encoded_mpf, mpf_buffer);

          DataRange new_mpf_range(0, encoded_mpf.size());
          new_mpf_segment = DataSegment::Create(new_mpf_range, mpf_buffer,
                                                DataSegment::kDelete);
        }
      }
    }
  }

  // Define replacements
  struct Replacement {
    DataRange old_range;
    shared_ptr<DataSegment> new_segment;
  };
  vector<Replacement> replacements;
  replacements.push_back({cut_range, xmp_segment});
  if (new_mpf_segment) {
    replacements.push_back({mpf_range, new_mpf_segment});
  }

  std::sort(replacements.begin(), replacements.end(),
            [](const Replacement& a, const Replacement& b) {
              return a.old_range.GetBegin() < b.old_range.GetBegin();
            });

  // Stream slices directly into output_sink without intermediate files
  size_t current_offset = image_range.GetBegin();

  for (const auto& rep : replacements) {
    if (rep.old_range.GetBegin() > current_offset) {
      size_t part_len = rep.old_range.GetBegin() - current_offset;
      if (!output_sink->TransferFrom(image_source, current_offset, part_len)) {
        return false;
      }
    }

    size_t rep_len = rep.new_segment->GetDataRange().GetLength();
    const Byte* rep_buf = rep.new_segment->GetBuffer(0);
    if (!output_sink->Write(rep_buf, rep_len)) {
      return false;
    }
    current_offset = rep.old_range.GetEnd();
  }

  if (current_offset < image_range.GetEnd()) {
    size_t rem_len = image_range.GetEnd() - current_offset;
    if (!output_sink->TransferFrom(image_source, current_offset, rem_len)) {
      return false;
    }
  }

  return true;
}

bool MotionPhotoJpegImageBuilder::AddImageFileAndMetadata(
    const string& image_file_name, std::string_view xmp_metadata) {
  auto source = MediaSource::FromFile(image_file_name, message_handler_);
  if (!source) {
    return false;
  }
  auto sink = DataSink::ToFile(output_file_name_, message_handler_);
  if (!sink) {
    return false;
  }
  return Build(*source, xmp_metadata, sink.get());
}

}  // namespace motion_photo
}  // namespace libmotionphoto
