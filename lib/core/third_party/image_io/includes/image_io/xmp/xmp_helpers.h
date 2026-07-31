#ifndef IMAGE_IO_XMP_XMP_HELPERS_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_HELPERS_H_  // NOLINT

#include <sstream>
#include <string>

namespace photos_editing_formats {
namespace image_io {

/// @param prefix The prefix to join with a colon and the name param.
/// @param name The name to joing with the prefix and colon.
/// @return A string with the format "prefix:name"
inline std::string JoinPrefixAndName(const std::string& prefix,
                                     const std::string& name) {
  std::stringstream ss;
  ss << prefix << ":" << name;
  return ss.str();
}

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_HELPERS_H_  // NOLINT
