#ifndef IMAGE_IO_JPEG_JPEG_GAIN_MAP_INFO_H_  // NOLINT
#define IMAGE_IO_JPEG_JPEG_GAIN_MAP_INFO_H_  // NOLINT

#include <string>

#include "image_io/base/data_range.h"

namespace photos_editing_formats {
namespace image_io {

/// The XMP HDR gain map namespace URIs for adobe and apple gain maps.
const char kAdobeGainMapUri[] = "http://ns.adobe.com/hdr-gain-map/1.0/";
const char kAppleGainMapUri[] = "http://ns.apple.com/HDRGainMap/1.0/";

/// Information about the HDR gain map embedded in a JPEG file.
struct JpegGainMapInfo {
  /// @return Whether the gain map info is valid.
  bool IsValid() const {
    return image_range.IsValid() &&
           (HasValidXmpMetadataRange() || HasValidIsoMetadataRange());
  }

  /// The equality operator.
  bool operator==(const JpegGainMapInfo& other) const {
    return image_range == other.image_range &&
           xmp_segment_range == other.xmp_segment_range &&
           xmp_namespace_uri == other.xmp_namespace_uri &&
           iso_segment_range == other.iso_segment_range;
  }

  /// The inequality operator.
  bool operator!=(const JpegGainMapInfo& other) const {
    return !(*this == other);
  }

  /// @return Whether the XMP metadata range and uri values are valid.
  bool HasValidXmpMetadataRange() const {
    return xmp_segment_range.IsValid() && !xmp_namespace_uri.empty();
  }

  /// @return Whether the ISO metadata range value is valid.
  bool HasValidIsoMetadataRange() const { return iso_segment_range.IsValid(); }

  /// The data range of the gain map in the containing data source.
  DataRange image_range;

  /// The data range of the jpeg segment containing the XMP data that describes
  /// the gain map.
  DataRange xmp_segment_range;

  /// The data range of the ISO 21496-1 HDR gain map metadata in the gain map
  /// image.
  DataRange iso_segment_range;

  /// The XMP namespace URI used to identify the XMP segment.
  std::string xmp_namespace_uri;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_JPEG_JPEG_GAIN_MAP_INFO_H_  // NOLINT
