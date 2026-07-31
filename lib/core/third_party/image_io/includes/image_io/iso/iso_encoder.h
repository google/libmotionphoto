#ifndef IMAGE_IO_ISO_ISO_ENCODER_H_  // NOLINT
#define IMAGE_IO_ISO_ISO_ENCODER_H_  // NOLINT

#include "image_io/iso/iso_types.h"

namespace photos_editing_formats {
namespace image_io {

/// A class that encodes various types of values according to the ISO 14496-12
/// (ISO base media file format, fifth edition - 2015-12-15) specification.
/// Section 4 of that specification dictates the use of a big-endian MSB-first
/// encoding). This class also supports the encoding a unsigned integer value in
/// variable number of bytes for an expandable "class" size as defined in the
/// ISO 14496-1 specification (Systems, fourth edition, 2010-06-01),
/// section 8.3.3. This IsoEncoder class refers to that type of encoding as a
/// "UIntX" value.
class IsoEncoder {
 public:
  /// @param value The value to determine the byte count of.
  /// @return The number of bytes required to represent the value in the ISO
  /// encoding implemented by the EncodeUIntXValue() functions.
  static size_t GetUIntXByteCount(UInt32 value);

  /// @return The vector of bytes to which encoded values are added.
  const UInt8Vector& GetBytes() const { return bytes_; }

  /// @param value The value to encode to the bytes vector.
  void EncodeUInt8Value(UInt8 value);

  /// @param value The value to encode to the bytes vector.
  void EncodeUInt32Value(UInt32 value);

  /// @param value The value to encode to the bytes vector.
  void EncodeUInt64Value(UInt64 value);

  /// @param value The value to encode to the bytes vector.
  void EncodeInt64Value(Int64 value);

  /// @param value The value to encode to the bytes vector.
  void EncodeFloatValue(float value);

  /// Calls the GetUIntXByteCount() to determine how many bytes are needed to
  /// encode the value, and calls other function of this name to append the
  /// bytes the bytes vector.
  /// @param value The value to encode to the bytes vector.
  void EncodeUIntXValue(UIntX value);

  /// @param value The value to encode to the bytes vector.
  /// @param index The index in the bytes vector at which to start placing the
  /// encoded bytes. If index is beyond the currentn size of the bytes vector,
  /// the bytes vector is resized so that index + count - 1 is a valid index.
  /// @param count The number of bytes allowed for the encoded bytes.
  /// @return Whether the value could be encoded in the given byte count.
  bool EncodeUIntXValue(UIntX value, size_t index, size_t count);

  /// @param count The number of bytes to reserve at the end of the bytes
  /// vector. I.e., the bytes vector's size is increased by count bytes, using
  /// the vector::resize(count, 0) function.
  size_t Reserve(size_t count);

  /// @param source_bytes The source bytes to append to the bytes vector.
  void Append(const UInt8Vector& source_bytes);

 private:
  /// The bytes vector.
  UInt8Vector bytes_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_ISO_ISO_ENCODER_H_  // NOLINT
