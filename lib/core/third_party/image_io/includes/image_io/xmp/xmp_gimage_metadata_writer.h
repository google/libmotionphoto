#ifndef IMAGE_IO_XMP_XMP_GIMAGE_METADATA_WRITER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_GIMAGE_METADATA_WRITER_H_  // NOLINT

#include <functional>

#include "image_io/xmp/xmp_gimage_metadata.h"
#include "image_io/xmp/xmp_writer_source.h"

namespace photos_editing_formats {
namespace image_io {

/// An XMP writer source for the GImage metadata.
/// Use an instance of this class with an XmpWriter.
class XmpGImageMetadataWriter : public XmpWriterSource {
 public:
  /// A typedef for a function used to write the large-valued Data value.
  using LargeValueWriter = std::function<void(
      const std::string& prefixed_name, const std::vector<DataRange>& ranges,
      image_io::XmlWriter* writer)>;

  /// @param metadata The container metadata to write.
  explicit XmpGImageMetadataWriter(const XmpGImageMetadata& metadata,
                                   LargeValueWriter large_value_writer);

  void WriteNamespaces(XmlWriter* writer) override;
  void WriteAttributeNamesAndValues(XmlWriter* writer) override;

 private:
  const XmpGImageMetadata& metadata_;
  LargeValueWriter large_value_writer_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_GIMAGE_METADATA_WRITER_H_  // NOLINT
