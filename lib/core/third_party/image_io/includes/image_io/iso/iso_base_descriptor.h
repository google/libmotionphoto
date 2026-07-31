#ifndef IMAGE_IO_ISO_ISO_BASE_DESCRIPTOR_H_  // NOLINT
#define IMAGE_IO_ISO_ISO_BASE_DESCRIPTOR_H_  // NOLINT

#include "image_io/iso/iso_decoder.h"
#include "image_io/iso/iso_encoder.h"

namespace photos_editing_formats {
namespace image_io {

/// A class that implements the BaseDescriptor type object in the ISO 14496-1
/// specification (Systems, fourth edition, 2010-06-01), section 7.2.2.2. The
/// object has the following SDL type syntax:
///
/// abstract aligned(8) expandable(2^28-1) class BaseDescriptor : bit(8) tag=0 {
///   // empty. To be filled by classes extending this class.
/// }
///
/// The possible tags are listed in the section 7.2.2.1 of the specification.
/// The 0xFF value is forbidden; values in the range [0xC0, 0xFE] can be used
/// by subclasses - they are considered "user private" tag values.
///
/// The expandable class size (aka the instance_size_ member variable) is
/// stored as a UIntX value between the object's UInt8 tag value and the first
/// byte of the BaseDescriptor's subclass's encoded bytes. The instance_size_
/// represents the number of bytes needed to encode the subclass' details.
class IsoBaseDescriptor {
 public:
  virtual ~IsoBaseDescriptor() = default;

  /// @param tag The tag to be associated with this instance of the descriptor.
  explicit IsoBaseDescriptor(UInt8 tag) : instance_size_(0), tag_(tag) {}

  /// @return The tag associated with this descriptor.
  UInt8 GetTag() const { return tag_; }

  /// @return The number of bytes that are to be decoded that represent the
  /// details of this descriptor. If the value of this descriptor were not
  /// based on the bytes decoded from a decoder, this value will be zero.
  UInt32 GetInstanceSize() const { return instance_size_; }

  /// Calls the DecodeTagValue() function to decodes the tag from the decoder
  /// check its value. If decoded tag matches the descriptor's expected tag
  /// value, it then calls the DecodeInstanceSizeAndDetails() function. Clients
  /// can call the decoder's IsValid() function to determine if the decoding was
  /// successful.
  /// @param decoder The decoder from which to obtain encoded bytes.
  void DecodeTagInstanceSizeAndDetails(IsoDecoder* decoder);

  /// Decodes the instance size and calls the DecodeDetails() function. Clients
  /// can call the decoder's IsValid() function to determine to determine if the
  /// decoding was successful.
  /// @param decoder The decoder from which to obtain encoded bytes.
  void DecodeInstanceSizeAndDetails(IsoDecoder* decoder);

  /// @return The name of the subclass, used for debug and warning messages.
  virtual const char* GetName() const = 0;

  /// Encodes the tag, instance size and details to the encoder. The instance
  /// size must be encoded using the EncodeUIntXValue() function.
  /// @param encoder The encoder to write encoded bytes to.
  virtual void Encode(image_io::IsoEncoder* encoder) const = 0;

  /// Decodes the details from the decoder. Subclasses that implement this
  /// function can assume that the decoder is position with its current index at
  /// the first byte of the encoded details. Clients can call the decoder's
  /// IsValid() function to determine to determine if the decoding was
  /// successful.
  /// @param decoder The decoder from which to obtain encoded bytes.
  virtual void DecodeDetails(IsoDecoder* decoder) = 0;

 private:
  UInt32 instance_size_;
  UInt8 tag_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_ISO_ISO_BASE_DESCRIPTOR_H_  // NOLINT
