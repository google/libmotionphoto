#ifndef IMAGE_IO_ISO_ISO_TYPES_H_  // NOLINT
#define IMAGE_IO_ISO_ISO_TYPES_H_  // NOLINT

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "image_io/base/types.h"

namespace photos_editing_formats {
namespace image_io {

/// The type for encoding a variable length unsigned integer.
using UIntX = UInt32;

/// The maximum number of bytes needed to encode a UIntX type value.
const UInt8 kMaxUIntXByteCount = 5;

// Constants used for encoding UIntX type values.
const int kUIntXDataBitCount = 7;
const UInt8 kUIntXDataMask = 0x7F;
const UInt8 kUIntXMoreBit = 0x80;

/// A vector of UInt8 values.
using UInt8Vector = std::vector<UInt8>;

/// A union of the various 32-bit values used when encoding/decoding signed
/// ints and floats.
union Union32 {
  float float_value;
  Int32 int32_value;
  UInt32 uint32_value;
};

/// A union of the various 64-bit values used when encoding/decoding signed
/// ints and doubles.
union Union64 {
  double double_value;
  Int64 int64_value;
  UInt64 uint64_value;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_ISO_ISO_TYPES_H_  // NOLINT
