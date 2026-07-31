#ifndef IMAGE_IO_XMP_XMP_GAIN_MAP_METADATA_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_GAIN_MAP_METADATA_H_  // NOLINT

#include <array>
#include <string>

#include "image_io/xmp/xmp_value.h"

namespace photos_editing_formats {
namespace image_io {

/// Constants for gain map metadata.
constexpr char kXmpGainMapPrefix[] = "hdrgm";
constexpr char kXmpGainMapUri[] = "http://ns.adobe.com/hdr-gain-map/1.0/";
constexpr char kXmpGainMapVersion[] = "Version";
constexpr char kXmpGainMapBaseRenditionIsHDR[] = "BaseRenditionIsHDR";
constexpr char kXmpGainMapGainMapMin[] = "GainMapMin";
constexpr char kXmpGainMapGainMapMax[] = "GainMapMax";
constexpr char kXmpGainMapGamma[] = "Gamma";
constexpr char kXmpGainMapOffsetSDR[] = "OffsetSDR";
constexpr char kXmpGainMapOffsetHDR[] = "OffsetHDR";
constexpr char kXmpGainMapHDRCapacityMin[] = "HDRCapacityMin";
constexpr char kXmpGainMapHDRCapacityMax[] = "HDRCapacityMax";

/// Current/default values.
constexpr char kXmpGainMapVersionCurrent[] = "1.0";

/// The metadata associated with an HDR gain map.
struct XmpGainMapMetadata {
  /// A simplifying typedef for the array types.
  using Float3 = std::array<float, 3>;

  /// The version of the gain map.
  XmpValue<std::string> version;

  /// Whether the base rendition is hdr.
  XmpValue<bool> base_rendition_is_hdr;

  /// The min value in the gain map.
  XmpValue<Float3> gain_map_min;

  /// The max value in the gain map.
  XmpValue<Float3> gain_map_max;

  /// The gamma value in the gain map.
  XmpValue<Float3> gamma;

  /// The SDR offset values.
  XmpValue<Float3> offset_sdr;

  /// The HDR offset values.
  XmpValue<Float3> offset_hdr;

  /// The min HDR capacity value.
  XmpValue<float> hdr_capacity_min;

  /// The max HDR capacity value.
  XmpValue<float> hdr_capacity_max;

  /// The equality operator.
  bool operator==(const XmpGainMapMetadata& rhs) const;

  /// The inequality operator.
  bool operator!=(const XmpGainMapMetadata& rhs) const {
    return !(*this == rhs);
  }

  /// Sets unassigned data members to default value.
  void SetUnassignedValuesToDefaults();
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_GAINMAP_METADATA_H_  // NOLINT
