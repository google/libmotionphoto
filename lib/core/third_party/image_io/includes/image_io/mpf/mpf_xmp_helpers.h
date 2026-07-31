#ifndef IMAGE_IO_MPF_MPF_XMP_HELPERS_H_  // NOLINT
#define IMAGE_IO_MPF_MPF_XMP_HELPERS_H_  // NOLINT

#include <cstddef>
#include <vector>

#include "image_io/base/message_handler.h"
#include "image_io/mpf/mpf_info.h"
#include "image_io/xmp/xmp_container_metadata.h"

namespace photos_editing_formats {
namespace image_io {

/// @param container_items The item metadata used to produce the mpf info.
/// @param message_handler An optional message handler to receive errors.
/// @return An MpfInfo with information obtained from the container metadata.
/// The MpfInfo.is_valid data member can be examined to determine if the info
/// is valid or not. There will always be at least two entries in a valid
/// structure. The image_offset values will be relative to the end of the
/// primary image. Consequently they will need to be adjusted to be relative
/// to the Mpf base point before the MpfInfo can be considered truly value And
/// written to the primary jpeg image.
MpfInfo BuildMpfInfo(
    const std::vector<XmpContainerItemMetadata>& container_items,
    MessageHandler* message_handler = nullptr);

/// @param mpf_info The mpf info to update the entry image_offsets of. These
/// offsets are assumed to be relative to the end of the primary image (as is
/// done by the BuildMpfInfo function).
/// @param primary_image_size The size of the primary image.
/// @param mpf_segment_offset The offset from the start of the image to the
/// APP2/MPF segment start.
void UpdateMpfInfoEntryImageOffsets(MpfInfo& mpf_info,
                                    size_t primary_image_size,
                                    size_t mpf_segment_offset);

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_MPF_MPF_XMP_HELPERS_H_  // NOLINT
