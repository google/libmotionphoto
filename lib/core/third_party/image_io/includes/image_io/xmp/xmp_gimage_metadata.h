#ifndef IMAGE_IO_XMP_XMP_GIMAGE_METADATA_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_GIMAGE_METADATA_H_  // NOLINT

#include <string>
#include <vector>

#include "image_io/base/data_range.h"
#include "image_io/xmp/xmp_value.h"

namespace photos_editing_formats {
namespace image_io {

/// Constants for GImage metadata.
const char kXmpGImagePrefix[] = "GImage";
const char kXmpGImageUri[] = "http://ns.google.com/photos/1.0/image/";
const char kXmpGImageMime[] = "Mime";
const char kXmpGImageData[] = "Data";

/// The metadata associated with an XMP GImage. Note that the data of the image
/// is reprsented as a range not a string, since these values can be large. See
/// https://developers.google.com/vr/reference/cardboard-camera-vr-photo-format#gimage
struct XmpGImageMetadata {
  bool operator==(const XmpGImageMetadata& rhs) const {
    return mime == rhs.mime && data_ranges == rhs.data_ranges;
  }
  bool operator!=(const XmpGImageMetadata& rhs) const {
    return !(*this == rhs);
  }
  XmpValue<std::string> mime;
  std::vector<DataRange> data_ranges;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_GIMAGE_METADATA_H_  // NOLINT
