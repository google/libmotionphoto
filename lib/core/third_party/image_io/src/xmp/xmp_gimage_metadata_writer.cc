#include "image_io/xmp/xmp_gimage_metadata_writer.h"

#include <utility>

#include "image_io/xmp/xmp_helpers.h"

namespace photos_editing_formats {
namespace image_io {

using std::string;

XmpGImageMetadataWriter::XmpGImageMetadataWriter(
    const XmpGImageMetadata& metadata, LargeValueWriter large_value_writer)
    : metadata_(metadata), large_value_writer_(std::move(large_value_writer)) {}

void XmpGImageMetadataWriter::WriteNamespaces(XmlWriter* writer) {
  writer->WriteXmlns(kXmpGImagePrefix, kXmpGImageUri);
}

void XmpGImageMetadataWriter::WriteAttributeNamesAndValues(XmlWriter* writer) {
  const string kMime = JoinPrefixAndName(kXmpGImagePrefix, kXmpGImageMime);
  MaybeWriteAttributeNameAndValue(kMime, metadata_.mime, writer);
  if (large_value_writer_) {
    const string kData = JoinPrefixAndName(kXmpGImagePrefix, kXmpGImageData);
    large_value_writer_(kData, metadata_.data_ranges, writer);
  }
}

}  // namespace image_io
}  // namespace photos_editing_formats
