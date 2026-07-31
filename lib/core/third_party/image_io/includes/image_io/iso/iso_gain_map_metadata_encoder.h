#ifndef IMAGE_IO_ISO_ISO_GAIN_MAP_METADATA_ENCODER_H_  // NOLINT
#define IMAGE_IO_ISO_ISO_GAIN_MAP_METADATA_ENCODER_H_  // NOLINT

#include <cstddef>
#include <vector>

#include "image_io/base/byte_array_encoder.h"
#include "image_io/base/types.h"
#include "image_io/iso/iso_gain_map_metadata.h"

namespace photos_editing_formats {
namespace image_io {

class IsoGainMapMetadataEncoder {
 public:
  /// The type of encoding to perform.
  enum EncodeType {
    kEncodeVersionOnly = 0,
    kEncodeAll = 1,
  };

  /// The type of prefix to use when encoding the metadata.
  enum PrefixType {
    kNoPrefix = 0,
    kUrnPrefix = 1,
    kApp2AndUrnPrefix = 2,
  };

  /// @param metadata The metadata to get the encoded size of.
  /// @param prefix_type The type of prefix to use when encoding the metadata.
  /// @param encode_type The type of encoding to perform.
  /// @return The number of bytes to encode the information in the metadata.
  static size_t GetEncodedSize(const IsoGainMapMetadata& metadata,
                               PrefixType prefix_type, EncodeType encode_type);

  /// @param metadata The metadata to encode.
  /// @param prefix_type The type of prefix to use when encoding the metadata.
  /// @param encode_type The type of encoding to perform.
  /// @return The encoded bytes. If the metadata is not valid an empty vector is
  /// returned.
  const std::vector<Byte>& Encode(const IsoGainMapMetadata& metadata,
                                  PrefixType prefix_type,
                                  EncodeType encode_type);

 private:
  ByteArrayEncoder encoder_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_ISO_ISO_GAIN_MAP_METADATA_ENCODER_H_  // NOLINT
