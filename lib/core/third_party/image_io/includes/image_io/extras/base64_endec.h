#ifndef IMAGE_IO_EXTRAS_BASE64_ENDEC_H_  // NOLINT
#define IMAGE_IO_EXTRAS_BASE64_ENDEC_H_  // NOLINT

#include <string>

namespace photos_editing_formats {
namespace image_io {

/// @param str The string to base64 encode in place.
void Base64Encode(std::string& str);

/// @param str The string to base64 encode.
/// @return The encoded string
std::string Base64Encode(const std::string& str);

/// @param str The string to base64 decode in place.
void Base64Decode(std::string& str);

/// @param str The string to decode.
/// @return The decoded string.
std::string Base64Decode(const std::string& str);

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_EXTRAS_BASE64_ENDEC_H_  // NOLINT
