#include "image_io/xmp/xmp_gimage_metadata_reader.h"

#include "image_io/xmp/xmp_helpers.h"

namespace photos_editing_formats {
namespace image_io {

using std::string;

XmpGImageMetadataReader::XmpGImageMetadataReader(
    XmpGImageMetadata* gimage_metadata)
    : gimage_metadata_(gimage_metadata) {
  AddUri(kXmpGImageUri);
}

void XmpGImageMetadataReader::SetUriPrefix(const string& uri,
                                           const string& prefix) {
  XmpContent::Type kValue = XmpContent::kValue;
  XmpContent::Type kRange = XmpContent::kRange;
  if (uri == kXmpGImageUri) {
    SetPrefix(kXmpGImagePrefix, prefix);
    AddSupportedName(JoinPrefixAndName(prefix, kXmpGImageMime), kValue);
    AddSupportedName(JoinPrefixAndName(prefix, kXmpGImageData), kRange);
  }
}

DataMatchResult XmpGImageMetadataReader::ProcessElementContent(
    const StringVector& element_name_stack, const string& element_name,
    const XmpContent& content, const XmlTokenContext& context) {
  if (IsPrefixedName(element_name, kXmpGImagePrefix, kXmpGImageMime)) {
    return SetXmpValue(content.value, element_name, context,
                       &gimage_metadata_->mime);
  } else if (IsPrefixedName(element_name, kXmpGImagePrefix, kXmpGImageData)) {
    return SetRanges(content.ranges, element_name, context,
                     &gimage_metadata_->data_ranges);
  }
  return context.GetResult();
}

}  // namespace image_io
}  // namespace photos_editing_formats
