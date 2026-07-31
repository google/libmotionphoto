#ifndef IMAGE_IO_JPEG_JPEG_MPF_INFO_CONSTANTS_H_  // NOLINT
#define IMAGE_IO_JPEG_JPEG_MPF_INFO_CONSTANTS_H_  // NOLINT

#include <cstddef>

#include "image_io/base/jpeg_app2_constants.h"
#include "image_io/base/types.h"

namespace photos_editing_formats {
namespace image_io {

constexpr Byte kMpfSig[] = {'M', 'P', 'F', '\0'};
constexpr size_t kMpfSigSize = sizeof(kMpfSig);
constexpr size_t kMpfEndianSize = 4;
constexpr Byte kMpfLittleEndian[kMpfEndianSize] = {0x49, 0x49, 0x2A, 0x00};
constexpr Byte kMpfBigEndian[kMpfEndianSize] = {0x4D, 0x4D, 0x00, 0x2A};
constexpr UInt32 kMpfTagSize = 12;
constexpr UInt32 kMpfTagSerializedCount = 3;

/// The offset from the start of an MPF jpeg segment to the start of the
/// MPF endian marker.
constexpr size_t kMpfEndianOffset =
    kApp2SigSize + kApp2LengthFieldSize + kMpfSigSize;

constexpr UInt16 kMpfTypeLong = 0x4;
constexpr UInt16 kMpfTypeUndefined = 0x7;

constexpr UInt16 kMpfVersionTag = 0xB000;
constexpr UInt16 kMpfVersionType = kMpfTypeUndefined;
constexpr UInt32 kMpfVersionCount = 4;
constexpr size_t kMpfVersionSize = 4;
constexpr Byte kMpfVersionExpected[kMpfVersionSize] = {'0', '1', '0', '0'};

constexpr UInt16 kMpfNumberOfImagesTag = 0xB001;
constexpr UInt16 kMpfNumberOfImagesType = kMpfTypeLong;
constexpr UInt32 kMpfNumberOfImagesCount = 1;

constexpr UInt16 kMpfEntryTag = 0xB002;
constexpr UInt16 kMpfEntryType = kMpfTypeUndefined;
constexpr UInt32 kMpfEntrySize = 16;
constexpr UInt16 kMpfIndividualImageUniqueIDTag = 0xB003;
constexpr UInt32 kMpfIndividualImageUniqueIDSize = 33;
constexpr UInt16 kMpfTotalNumberCapturedFramesTag = 0xB004;
constexpr UInt32 kMpfTotalNumberCaptureFramesCount = 1;

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_JPEG_JPEG_MPF_INFO_CONSTANTS_H_  // NOLINT
