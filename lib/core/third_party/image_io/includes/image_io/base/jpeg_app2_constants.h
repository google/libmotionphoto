#ifndef IMAGE_IO_BASE_JPEG_APP2_CONSTANTS_H_  // NOLINT
#define IMAGE_IO_BASE_JPEG_APP2_CONSTANTS_H_  // NOLINT

#include <cstddef>

#include "image_io/base/types.h"

namespace photos_editing_formats {
namespace image_io {

// The APP2 signature for such a segment in a JPEG file.
constexpr Byte kApp2Sig[] = {0xFF, 0xE2};
constexpr size_t kApp2SigSize = sizeof(kApp2Sig);
constexpr size_t kApp2LengthFieldSize = 2;

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_BASE_JPEG_APP2_CONSTANTS_H_  // NOLINT
