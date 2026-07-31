#include "image_io/iso/iso_decoder.h"

#include <sstream>

#include "image_io/base/byte_data.h"
#include "image_io/iso/iso_types.h"

namespace photos_editing_formats {
namespace image_io {

void IsoDecoder::Reset(const UInt8* bytes, size_t begin, size_t end) {
  bytes_ = bytes;
  range_ = DataRange(begin, end);
  next_ = begin;
  read_past_end_count_ = 0;
  unexpected_tag_count_ = 0;
  incomplete_decoding_count_ = 0;
  verbose_messages_.clear();
}

IsoDecoder IsoDecoder::GetSlice(size_t begin, size_t end) const {
  DataRange range = range_.GetIntersection(DataRange(begin, end));
  IsoDecoder decoder(bytes_, range.GetBegin(), range.GetEnd());
  decoder.SetVerbose(is_verbose_);
  return decoder;
}

void IsoDecoder::CaptureSliceCountsAndMessages(const IsoDecoder& slice) {
  read_past_end_count_ += slice.read_past_end_count_;
  unexpected_tag_count_ += slice.unexpected_tag_count_;
  incomplete_decoding_count_ += slice.incomplete_decoding_count_;
  if (is_verbose_ && !slice.verbose_messages_.empty()) {
    verbose_messages_.insert(verbose_messages_.end(),
                             slice.verbose_messages_.begin(),
                             slice.verbose_messages_.end());
  }
}

UInt8 IsoDecoder::DecodeUInt8Value() {
  if (next_ >= GetEnd()) {
    read_past_end_count_++;
    return 0;
  }
  return bytes_[next_++];
}

UInt32 IsoDecoder::DecodeUInt32Value() {
  UInt32 value = 0;
  if (next_ > GetEnd() || sizeof(UInt32) > GetEnd() - next_) {
    read_past_end_count_++;
    return value;
  }
  const UInt8* bytes = bytes_ + next_;
  auto SL = [](UInt32 value, int shift) { return value << shift; };
  value = SL(bytes[0], 24) | SL(bytes[1], 16) | SL(bytes[2], 8) | bytes[3];
  next_ += sizeof(UInt32);
  return value;
}

UInt64 IsoDecoder::DecodeUInt64Value() {
  UInt64 value = 0;
  if (next_ > GetEnd() || sizeof(UInt64) > GetEnd() - next_) {
    read_past_end_count_++;
    return value;
  }
  const UInt8* bytes = bytes_ + next_;
  auto SL = [](UInt64 value, int shift) { return value << shift; };
  value = SL(bytes[0], 56) | SL(bytes[1], 48) | SL(bytes[2], 40) |
          SL(bytes[3], 32) | SL(bytes[4], 24) | SL(bytes[5], 16) |
          SL(bytes[6], 8) | bytes[7];
  next_ += sizeof(UInt64);
  return value;
}

Int64 IsoDecoder::DecodeInt64Value() {
  Union64 union64;
  union64.uint64_value = DecodeUInt64Value();
  return union64.int64_value;
}

float IsoDecoder::DecodeFloatValue() {
  Union32 union32;
  union32.uint32_value = DecodeUInt32Value();
  return union32.float_value;
}

UIntX IsoDecoder::DecodeUIntXValue() {
  bool more = true;
  UIntX value = 0;
  UInt8 bytes_consumed = 0;
  for (size_t index = next_;
       index < GetEnd() && more && bytes_consumed < kMaxUIntXByteCount;
       ++index) {
    const UInt8& byte = bytes_[index];
    value = (value << kUIntXDataBitCount) | (byte & kUIntXDataMask);
    more = ((byte & kUIntXMoreBit) == kUIntXMoreBit);
    bytes_consumed += 1;
  }
  if (more) {
    value = 0;
    read_past_end_count_++;
  } else {
    next_ += bytes_consumed;
  }
  return value;
}

bool IsoDecoder::DecodeTagValue(UInt8 expected_tag, const char* class_name) {
  UInt8 tag = DecodeUInt8Value();
  if (tag != expected_tag) {
    if (IsVerbose()) {
      std::stringstream ss;
      ss << "Unrecognized tag: 0x" << ByteData::Byte2Hex(tag) << " expected ";
      if (class_name) {
        ss << class_name << ":";
      }
      ss << "0x" << ByteData::Byte2Hex(expected_tag) << " at index "
         << next_ - 1 << " in range [" << range_.GetBegin() << ":"
         << range_.GetEnd() << ")";
      verbose_messages_.push_back(ss.str());
    }
    IncrementUnexpectedTagCount(1);
    return false;
  }
  return true;
}

}  // namespace image_io
}  // namespace photos_editing_formats
