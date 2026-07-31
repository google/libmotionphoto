#include "image_io/base/byte_array_encoder.h"

#include <vector>

namespace photos_editing_formats {
namespace image_io {

void ByteArrayEncoder::EncodeUInt8Value(UInt8 value) {
  bytes_.emplace_back(value);
}

void ByteArrayEncoder::EncodeUInt16Value(UInt16 value) {
  bytes_.emplace_back(static_cast<UInt8>((value >> 8) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>(value & 0xFF));
}

void ByteArrayEncoder::EncodeUInt32Value(UInt32 value) {
  bytes_.emplace_back(static_cast<UInt8>((value >> 24) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 16) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 8) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>(value & 0xFF));
}

void ByteArrayEncoder::EncodeUInt64Value(UInt64 value) {
  bytes_.emplace_back(static_cast<UInt8>((value >> 56) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 48) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 40) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 32) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 24) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 16) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>((value >> 8) & 0xFF));
  bytes_.emplace_back(static_cast<UInt8>(value & 0xFF));
}

void ByteArrayEncoder::Append(const ByteVector& source_bytes) {
  bytes_.insert(bytes_.end(), source_bytes.begin(), source_bytes.end());
}

void ByteArrayEncoder::Append(const Byte* source_bytes, size_t source_size) {
  bytes_.insert(bytes_.end(), source_bytes, source_bytes + source_size);
}

}  // namespace image_io
}  // namespace photos_editing_formats
