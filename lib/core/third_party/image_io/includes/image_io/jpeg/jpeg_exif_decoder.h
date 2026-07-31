#ifndef IMAGE_IO_JPEG_JPEG_EXIF_DECODER_H_  // NOLINT
#define IMAGE_IO_JPEG_JPEG_EXIF_DECODER_H_  // NOLINT

#include <cstddef>
#include <string>
#include <vector>

#include "image_io/base/byte_array_decoder.h"
#include "image_io/base/image_metadata.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/types.h"

namespace photos_editing_formats {
namespace image_io {

/// A very simple decoder for the Exif data found in a JPEG file. The only
/// values decoded are represented in an image_io::ImageMetadata object.
/// For an exif decoder and the specification, see:
/// http://www.cipa.jp/std/documents/e/DC-008-Translation-2016-E.pdf
class JpegExifDecoder : public ByteArrayDecoder {
 public:
  /// @param image_metadata The metadata to receive the decoded values.
  /// @param message_handler An optional message handler used to report error
  /// and warning conditions encountered while decoding the bytes.
  JpegExifDecoder(ImageMetadata* image_metadata,
                  MessageHandler* message_handler);

  /// @param bytes The bytes to decode.
  /// @param count The number of bytes to decode.
  /// @return Whether the bytes were deocded successfully.
  bool Decode(const Byte* bytes, size_t count);

  /// @param bytes The bytes to decode.
  /// @return Whether the bytes were deocded successfully.
  bool Decode(const std::vector<Byte>& bytes);

 private:
  bool GetIfdIndex(size_t* index, UInt32* value);
  bool GetIfdEntryCount(size_t* index, UInt16* count);
  bool GetIfdEntry(size_t* index, UInt16* tag, UInt16* type, UInt32* count);
  bool GetIfdEntryNumericValue(UInt16 type, size_t* index, UInt32* value);
  void SkipOptionalJpegSegmentStuff(size_t* index);
  bool GetEndian(size_t* index);
  bool SetOrientation(UInt16 type, size_t* index);
  ImageMetadata* image_metadata_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_JPEG_JPEG_EXIF_DECODER_H_  // NOLINT
