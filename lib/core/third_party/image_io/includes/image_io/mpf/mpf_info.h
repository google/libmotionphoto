#ifndef IMAGE_IO_JPEG_JPEG_MPF_INFO_H_  // NOLINT
#define IMAGE_IO_JPEG_JPEG_MPF_INFO_H_  // NOLINT

#include <cstddef>
#include <vector>

#include "image_io/base/types.h"

namespace photos_editing_formats {
namespace image_io {

/// The values and masks for use with the MpfEntry::attribute data member.
enum MpfEntryAttribute {
  /// The mask for the format value.
  kMpfEntryAttributeFormatMask = 0x7000000,

  /// The format value that indicates a JPEG image.
  kMpfEntryAttributeFormatJpeg = 0x0000000,

  /// The mask for the image type bits.
  kMpfEntryAttributeTypeMask = 0xFFFFFF,

  /// The type value that indicates the primary image.
  kMpfEntryAttributeTypePrimary = 0x030000,
};

/// The information about an image in an MpfInfo structure.
struct MpfEntry {
  /// The image attribute bits, some combination of the format and values
  /// defined in the MpfEntryAttribute enum.
  UInt32 attribute = 0;

  /// The size of the image in bytes.
  UInt32 image_size = 0;

  /// The offset of the image in bytes. This offset is specified relative to
  /// the address of the MP Endian field in the MP Header, unless the image is
  /// a First Individual Image, in which case the value of the offset shall be
  /// NULL (from section 5.2.3.3).
  UInt32 image_offset = 0;

  /// The dependent image entry numbers.
  UInt16 dependent_image1_entry_number = 0;
  UInt16 dependent_image2_entry_number = 0;
};

/// The Multi-Picture Format structure as specified by CIPA DC-007.
struct MpfInfo {
  /// Whether the info in this structure is valid.
  bool is_valid = false;

  /// The entries listed in the Index Image File Directory.
  std::vector<MpfEntry> entries;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_JPEG_JPEG_MPF_INFO_H_  // NOLINT
