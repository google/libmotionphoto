#include "image_io/iso/iso_encoder.h"

#include <algorithm>

namespace photos_editing_formats {
namespace image_io {

size_t IsoEncoder::GetUIntXByteCount(UInt32 value) {
  UIntX candidate_value = kUIntXDataMask;
  for (UInt8 count = 1; count < kMaxUIntXByteCount; ++count) {
    if (value <= candidate_value) {
      return count;
    }
    candidate_value = (candidate_value << kUIntXDataBitCount) | kUIntXDataMask;
  }
  return kMaxUIntXByteCount;
}

void IsoEncoder::EncodeUInt8Value(UInt8 value) { bytes_.emplace_back(value); }

void IsoEncoder::EncodeUInt32Value(UInt32 value) {
  bytes_.emplace_back(static_cast<UInt8>((value >> 24) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 16) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 8) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>(value & 0xFF));
}

void IsoEncoder::EncodeUInt64Value(UInt64 value) {
  bytes_.emplace_back(static_cast<UInt8>((value >> 56) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 48) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 40) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 32) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 24) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 16) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 8) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>(value & 0xFF));
}

void IsoEncoder::EncodeInt64Value(Int64 value) {
  Union64 union64;
  union64.int64_value = value;
  EncodeUInt64Value(union64.uint64_value);
}

void IsoEncoder::EncodeFloatValue(float value) {
  Union32 union32;
  union32.float_value = value;
  EncodeUInt32Value(union32.uint32_value);
}

bool IsoEncoder::EncodeUIntXValue(UIntX value, size_t index, size_t count) {
  if (count == 0) {
    return false;
  }
  const size_t sindex = index;
  const size_t eindex = index + count;
  if (bytes_.size() < index + count) {
    bytes_.resize(index + count, 0);
  }
  size_t byte_count = 1;
  bytes_[index++] = static_cast<UInt8>(value & kUIntXDataMask);
  value = value >> kUIntXDataBitCount;
  while (value != 0 && byte_count < count) {
    bytes_[index++] =
        static_cast<UInt8>(value & kUIntXDataMask) | kUIntXMoreBit;
    value = value >> kUIntXDataBitCount;
    byte_count += 1;
  }
  if (value == 0) {
    while (byte_count < count) {
      bytes_[index++] = kUIntXMoreBit;
      byte_count += 1;
    }
  }
  std::reverse(bytes_.begin() + sindex, bytes_.begin() + eindex);
  return value == 0;
}

void IsoEncoder::EncodeUIntXValue(UIntX value) {
  EncodeUIntXValue(value, bytes_.size(), GetUIntXByteCount(value));
}

size_t IsoEncoder::Reserve(size_t count) {
  size_t index = bytes_.size();
  bytes_.insert(bytes_.end(), count, 0);
  return index;
}

void IsoEncoder::Append(const UInt8Vector& source_bytes) {
  bytes_.insert(bytes_.end(), source_bytes.begin(), source_bytes.end());
}

}  // namespace image_io
}  // namespace photos_editing_formats
