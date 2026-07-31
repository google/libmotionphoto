#ifndef IMAGE_IO_XMP_XMP_GIMAGE_READER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_GIMAGE_READER_H_  // NOLINT

#include "image_io/xmp/xmp_content_handler.h"
#include "image_io/xmp/xmp_gimage_metadata.h"

namespace photos_editing_formats {
namespace image_io {

/// An XMP content handler for the Container and Container:Item metadata.
/// Use an instance of this class with an XmpReader.
class XmpGImageMetadataReader : public XmpContentHandler {
 public:
  /// @param gimage_metadata The metadata to be filled in by the reader.
  explicit XmpGImageMetadataReader(XmpGImageMetadata* gimage_metadata);
  void SetUriPrefix(const std::string& uri, const std::string& prefix) override;
  DataMatchResult ProcessElementContent(
      const StringVector& element_name_stack, const std::string& element_name,
      const XmpContent& content, const XmlTokenContext& context) override;

 private:
  XmpGImageMetadata* gimage_metadata_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_GIMAGE_READER_H_  // NOLINT
