#ifndef IMAGE_IO_MPF_MPF_INFO_ENCODER_H_  // NOLINT
#define IMAGE_IO_MPF_MPF_INFO_ENCODER_H_  // NOLINT

#include <vector>

#include "image_io/base/byte_array_encoder.h"
#include "image_io/base/types.h"
#include "image_io/mpf/mpf_info.h"

namespace photos_editing_formats {
namespace image_io {

/// An encoder for Multi Picture Format data.
class MpfInfoEncoder {
 public:
  /// @param mpf_info The MpfInfo structure to encode.
  /// @param include_app2_prefix Whether to include the four bytes needed for
  /// the JPEG APP2 segment prefix in the size that is returned.
  /// @return The number of bytes to encode the information in the MpfInfo. This
  /// includes the MPF signature and optionally the size of the App2 prefix.
  static size_t GetEncodedSize(const MpfInfo& mpf_info,
                               bool include_app2_prefix = false);

  /// @param map_info The MpfInfo structure to encode.
  /// @param include_app2_prefix Whether to include four bytes at the start of
  /// the resulting bytes that indicate a JPEG APP2 segment.
  /// @return The bytes of the encoded MpfInfo structure.
  const std::vector<Byte>& Encode(const MpfInfo& mpf_info,
                                  bool include_app2_prefix = false);

 private:
  ByteArrayEncoder encoder_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_MPF_MPF_INFO_ENCODER_H_  // NOLINT
