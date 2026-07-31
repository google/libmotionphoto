#ifndef IMAGE_IO_XMP_XMP_CONTAINER_METADATA_WRITER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_CONTAINER_METADATA_WRITER_H_  // NOLINT

#include "image_io/xmp/xmp_container_metadata.h"
#include "image_io/xmp/xmp_writer_source.h"

namespace photos_editing_formats {
namespace image_io {

/// An XMP writer source for the Container and Container:Item metadata.
/// Use an instance of this class with an XmpWriter.
class XmpContainerMetadataWriter : public XmpWriterSource {
 public:
  /// @param metadata The container metadata to write.
  explicit XmpContainerMetadataWriter(const XmpContainerMetadata& metadata);

  void WriteNamespaces(XmlWriter* writer) override;
  void WriteAttributeNamesAndValues(XmlWriter* writer) override;
  void WriteElements(XmlWriter* writer) override;

 private:
  const XmpContainerMetadata& metadata_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_CONTAINER_METADATA_WRITER_H_  // NOLINT
