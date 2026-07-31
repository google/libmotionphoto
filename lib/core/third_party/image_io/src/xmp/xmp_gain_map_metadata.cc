#include "image_io/xmp/xmp_gain_map_metadata.h"

#include <array>
#include <cmath>

#include "image_io/xmp/xmp_value.h"

namespace photos_editing_formats {
namespace image_io {

namespace {
using Float3 = std::array<float, 3>;

constexpr std::array<float, 3> kZeroArray = {0., 0., 0.};
constexpr std::array<float, 3> kOneArray = {1., 1., 1.};
constexpr std::array<float, 3> kOne64thArray = {1. / 64., 1. / 64., 1. / 64.};

//   a: The first value to compare.
//   b: The second value to compare.
// Returns whether the a and b values are close enough for gov'mt work.
bool IsNear(float a, float b) { return std::fabs(a - b) < 1.e-6; }

//   lhs: The first value to compare.
//   rhs: The second value to compare.
// Returns whether the xmp values are equal or nearly so.
bool IsEqual(const XmpValue<Float3>& lhs, const XmpValue<Float3>& rhs) {
  return lhs.IsValid() == rhs.IsValid() &&
         lhs.WasAssigned() == rhs.WasAssigned() &&
         IsNear(lhs.GetValue()[0], rhs.GetValue()[0]) &&
         IsNear(lhs.GetValue()[1], rhs.GetValue()[1]) &&
         IsNear(lhs.GetValue()[2], rhs.GetValue()[2]);
}
}  // namespace

bool XmpGainMapMetadata::operator==(const XmpGainMapMetadata& rhs) const {
  return version == rhs.version &&
         base_rendition_is_hdr == rhs.base_rendition_is_hdr &&
         IsEqual(gain_map_min, rhs.gain_map_min) &&
         IsEqual(gain_map_max, rhs.gain_map_max) && IsEqual(gamma, rhs.gamma) &&
         IsEqual(offset_sdr, rhs.offset_sdr) &&
         IsEqual(offset_hdr, rhs.offset_hdr) &&
         hdr_capacity_min == rhs.hdr_capacity_min &&
         hdr_capacity_max == rhs.hdr_capacity_max;
}

void XmpGainMapMetadata::SetUnassignedValuesToDefaults() {
  version.SetValueIfUnassigned(kXmpGainMapVersionCurrent);
  base_rendition_is_hdr.SetValueIfUnassigned(false);
  gain_map_min.SetValueIfUnassigned(kZeroArray);
  gain_map_max.SetValueIfUnassigned(kOneArray);
  gamma.SetValueIfUnassigned(kOneArray);
  offset_sdr.SetValueIfUnassigned(kOne64thArray);
  offset_hdr.SetValueIfUnassigned(kOne64thArray);
  hdr_capacity_min.SetValueIfUnassigned(0.);
  hdr_capacity_max.SetValueIfUnassigned(1.);
}

}  // namespace image_io
}  // namespace photos_editing_formats
