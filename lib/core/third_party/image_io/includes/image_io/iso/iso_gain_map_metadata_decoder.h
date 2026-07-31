#ifndef IMAGE_IO_ISO_ISO_GAIN_MAP_METADATA_DECODER_H_  // NOLINT
#define IMAGE_IO_ISO_ISO_GAIN_MAP_METADATA_DECODER_H_  // NOLINT

#include <cstddef>
#include <string>

#include "image_io/base/byte_array_decoder.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/types.h"
#include "image_io/iso/iso_gain_map_metadata.h"

namespace photos_editing_formats {
namespace image_io {

/// A decoder for the ISO 21496-1 gain map metadata.
class IsoGainMapMetadataDecoder : public ByteArrayDecoder {
 public:
  /// The type of decoding to perform.
  enum DecodeType {
    kDecodeVersionOnly = 0,
    kDecodeAll = 1,
  };

  /// @return The metadata that was decoded.
  const IsoGainMapMetadata& GetMetadata() const { return metadata_; }

  /// @param message_handler An optional message handler used to report error
  /// and warning conditions encountered while decoding the bytes.
  explicit IsoGainMapMetadataDecoder(MessageHandler* message_handler);

  /// Return the number of bytes to skip if the bytes data begins with a JPEG
  /// APP2 identifier and length field (i.e., FFE2xxxx). Using the return value
  /// of this function you can increment your bytes pointer and decrement your
  /// count value and call the SkipUrnIdentifier function.
  /// @param bytes The bytes to examine for the JPEG/APP2 type identifier.
  /// @param count The number of bytes in the bytes buffer.
  /// @param required Whether to report an error if the APP2 identifier is not
  /// found (you can check the MessageHandler::HasErrorMessages to verify if
  /// an error message was issued.)
  /// @return The number of bytes to be skipped to skip the APP1 prefix.  size_t
  /// SkipApp2Identifier(const Byte* bytes, size_t count,
  size_t SkipApp2Identifier(const Byte* bytes, size_t count,
                            bool required = false);

  /// Return the number of bytes to skip if the bytes data begins with the URN
  /// identifier. Using the return value of this function you can increment your
  /// bytes pointer and decrement your count value. This will result in the
  /// bytes pointer pointing to the start of the metadata value. You can use the
  /// bytes pointer and count to call the Decode function.
  /// @param bytes The bytes to examine for the URN type identifier.
  /// @param count The number of bytes in the bytes buffer.
  /// @param required Whether to report an error if the URN identifier is not
  /// found (you can check the MessageHandler::HasErrorMessages to verify if
  /// an error message was issued.)
  /// @return The number of bytes to be skipped to skip the URN identifier.
  size_t SkipUrnIdentifier(const Byte* bytes, size_t count,
                           bool required = false);

  /// @param bytes The bytes to decode.
  /// @param count The number of bytes to decode.
  /// @param decode_type The type of decode to perform.
  /// @return Whether the bytes were decoded successfully.
  bool Decode(const Byte* bytes, size_t count, DecodeType decode_type);

 private:
  bool DecodeVersion(size_t* index, IsoGainMapVersion* version);
  bool DecodeChannelMetadata(const std::string& channel_prefix, size_t* index,
                             IsoGainMapChannelMetadata* channel_metadata);
  bool DecodeSignedNumeratorAndDenominator(const std::string& expecting,
                                           size_t* index, Int32* numerator,
                                           UInt32* denominator);
  bool DecodeUnsignedNumeratorAndDenominator(const std::string& expecting,
                                             size_t* index, UInt32* numerator,
                                             UInt32* denominator);
  IsoGainMapMetadata metadata_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_ISO_ISO_GAIN_MAP_METADATA_DECODER_H_  // NOLINT
