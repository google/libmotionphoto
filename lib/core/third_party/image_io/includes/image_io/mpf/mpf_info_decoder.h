#ifndef IMAGE_IO_MPF_MPF_INFO_DECODER_H_  // NOLINT
#define IMAGE_IO_MPF_MPF_INFO_DECODER_H_  // NOLINT

#include <cstddef>
#include <vector>

#include "image_io/base/byte_array_decoder.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/types.h"
#include "image_io/mpf/mpf_info.h"

namespace photos_editing_formats {
namespace image_io {

/// A decoder for Multi Picture Format data.
class MpfInfoDecoder : public ByteArrayDecoder {
 public:
  /// @param message_handler An optional message handler used to report error
  /// and warning conditions encountered while decoding the bytes.
  explicit MpfInfoDecoder(MessageHandler* message_handler)
      : ByteArrayDecoder(message_handler) {}

  /// @return The MpfInfo with the image info.
  const MpfInfo& GetInfo() const { return mpf_info_; }

  /// Return the number of bytes to skip if the bytes data begins with a JPEG
  /// APP2 identifier and length field (i.e., FFE2xxxx). Using the return value
  /// of this function you can update the increment your bytes pointer and
  /// decrement your count value and call the SkipMpfPrefix function.
  /// @param bytes The bytes to examine for the JPEG/APP2 type identifier.
  /// @param count The number of bytes in the bytes buffer.
  /// @param required Whether to report an error if the APP2 identifier is not
  /// found (you can check the MessageHandler::HasErrorMessages to verify if
  /// an error message was issued.)
  /// @return The number of bytes to be skipped to skip the APP1 prefix.
  size_t SkipApp2Identifier(const Byte* bytes, size_t count,
                            bool required = false);

  /// Return the number of bytes to skip if the bytes data begins with the MPF
  /// identifier. Using the return value of this function you can update the
  /// increment your bytes pointer and decrement your count value. This will
  /// result in the bytes pointer pointing to the start of the MPF endian value.
  /// You can use the bytes pointer and count to call the Decode function.
  /// @param bytes The bytes to examine for the MPF type identifier.
  /// @param count The number of bytes in the bytes buffer.
  /// @param required Whether to report an error if the MPF identifier is not
  /// found (you can check the MessageHandler::HasErrorMessages to verify if
  /// an error message was issued.)
  /// @return The number of bytes to be skipped to skip the MPF prefix.
  size_t SkipMpfIdentifier(const Byte* bytes, size_t count,
                           bool required = false);

  /// @param bytes The bytes to decode. This should be the byte at which the
  /// MPF endian definition starts (and the point that serves as the base of
  /// the entry image offset values).
  /// @param count The number of bytes to decode.
  /// @return Whether the bytes were decoded successfully.
  bool Decode(const Byte* bytes, size_t count);

 private:
  MpfInfo mpf_info_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_MPF_MPF_INFO_DECODER_H_  // NOLINT
