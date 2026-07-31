#ifndef IMAGE_IO_BASE_BYTE_ARRAY_ENCODER_H_  // NOLINT
#define IMAGE_IO_BASE_BYTE_ARRAY_ENCODER_H_  // NOLINT

#include <vector>

#include "image_io/base/types.h"

namespace photos_editing_formats {
namespace image_io {

// A class that encodes various types of values into a byte vector using a
// Big endian encoding scheme.
class ByteArrayEncoder {
 public:
  // Short hand typedef.
  using ByteVector = std::vector<Byte>;

  // Returns the vector of bytes to which encoded values are added.
  const ByteVector& GetBytes() const { return bytes_; }

  //   value: The value to encode to the bytes vector.
  void EncodeUInt8Value(UInt8 value);

  //   value: The value to encode to the bytes vector.
  void EncodeUInt16Value(UInt16 value);

  //   value: The value to encode to the bytes vector.
  void EncodeUInt32Value(UInt32 value);

  //   value: The value to encode to the bytes vector.
  void EncodeUInt64Value(UInt64 value);

  //   source_bytes: The source bytes to append to the bytes vector.
  void Append(const ByteVector& source_bytes);

  //   source_bytes: The source bytes to append to the bytes vector.
  //   source_size: The number of bytes to append to the bytes vector.
  void Append(const Byte* source_bytes, size_t source_size);

  // Returns the writable vector of bytes.
  ByteVector& GetMutableBytes() { return bytes_; }

 private:
  // The bytes vector.
  ByteVector bytes_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_BASE_BYTE_ARRAY_ENCODER_H_  // NOLINT
