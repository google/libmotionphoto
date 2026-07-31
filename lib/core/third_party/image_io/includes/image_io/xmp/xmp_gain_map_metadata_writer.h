#ifndef IMAGE_IO_XMP_XMP_GAIN_MAP_METADATA_WRITER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_GAIN_MAP_METADATA_WRITER_H_  // NOLINT

#include <string>

#include "image_io/xml/xml_writer.h"
#include "image_io/xmp/xmp_gain_map_metadata.h"
#include "image_io/xmp/xmp_writer_source.h"

namespace photos_editing_formats {
namespace image_io {

/// An XMP writer source for the XMP gain map metadata.
/// Use an instance of this class with an XmpWriter.
class XmpGainMapMetadataWriter : public XmpWriterSource {
 public:
  /// @param metadata The gain map metadata to write.
  explicit XmpGainMapMetadataWriter(const XmpGainMapMetadata& metadata);

  void WriteNamespaces(XmlWriter* writer) override;
  void WriteAttributeNamesAndValues(XmlWriter* writer) override;
  void WriteElements(XmlWriter* writer) override;

 private:
  enum WritePhase {
    kWriteAttributesPhase,
    kWriteElementsPhase,
  };
  void WriteFloat3Values(WritePhase phase, XmlWriter* writer);
  void WriteFloat3Value(WritePhase phase, const std::string& name,
                        const XmpValue<XmpGainMapMetadata::Float3>& value,
                        XmlWriter* writer);
  const XmpGainMapMetadata& metadata_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_GAIN_MAP_METADATA_WRITER_H_  // NOLINT
