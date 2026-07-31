#include "image_io/extras/base64_endec.h"

#include <string>

#include "modp_b64.h"

namespace photos_editing_formats {
namespace image_io {

void Base64Encode(std::string& str) { modp::b64_encode(str); }

std::string Base64Encode(const std::string& str) {
  return modp::b64_encode(str);
}

void Base64Decode(std::string& str) { modp::b64_decode(str); }

std::string Base64Decode(const std::string& str) {
  return modp::b64_decode(str);
}

}  // namespace image_io
}  // namespace photos_editing_formats
