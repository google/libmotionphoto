#ifndef IMAGE_IO_ISO_ISO_GAIN_MAP_CONSTANTS_H_  // NOLINT
#define IMAGE_IO_ISO_ISO_GAIN_MAP_CONSTANTS_H_  // NOLINT

#include <cstddef>

#include "image_io/base/types.h"

namespace photos_editing_formats {
namespace image_io {

// The current maximum supported version of the ISO 21496-1 gain map metadata
// supported by this code.
constexpr UInt16 kIsoGainMapCurrentMaxSupportedVersion = 0;

// The current writer version of the ISO 21496-1 gain map metadata supported by
// this code.
constexpr UInt16 kIsoGainMapCurrentWriterVersion = 0;

// The URN for the ISO 21496-1 gain map metadata.
constexpr char kIsoGainMapMetadataURN[] = "urn:iso:std:iso:ts:21496:-1";

/// The encoded size of the ISO 21496-1 gain map metadata version data.
constexpr size_t kIsoGainMapVersionEncodedSize = 4;

// The encoded size of the ISO 21496-1 gain map channel metadata.
constexpr size_t kIsoGainMapChannelEncodedSize = 40;

// The encoded size of the ISO 21496-1 gain map main metadata, excluding the
// version and channel metadata.
constexpr size_t kIsoGainMapMainMetadataEncodedSize = 17;

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_ISO_ISO_GAIN_MAP_CONSTANTS_H_   // NOLINT
