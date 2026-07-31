#ifndef IMAGE_IO_ISO_ISO_GAIN_MAP_METADATA_H_  // NOLINT
#define IMAGE_IO_ISO_ISO_GAIN_MAP_METADATA_H_  // NOLINT

#include <algorithm>
#include <cstddef>
#include <vector>

#include "image_io/base/types.h"
#include "image_io/iso/iso_gain_map_constants.h"

namespace photos_editing_formats {
namespace image_io {

// The version of the ISO 21496-1 gain map metadata.
struct IsoGainMapVersion {
  // The minimum version of the ISO 21496-1 gain map metadata supported by this
  // instance.
  UInt16 minimum_version = kIsoGainMapCurrentMaxSupportedVersion;

  // The version of the ISO 21496-1 gain map metadata that was read or will be
  // written for this instance.
  UInt16 writer_version = kIsoGainMapCurrentWriterVersion;

  // @return Whether the version is valid.
  bool IsValid() const { return minimum_version <= writer_version; }

  // @return Whether the version is supported by this code.
  bool IsSupported() const {
    return minimum_version <= kIsoGainMapCurrentMaxSupportedVersion;
  }

  // @return Whether the version is equal to the rhs version.
  bool operator==(const IsoGainMapVersion& rhs) const {
    return minimum_version == rhs.minimum_version &&
           writer_version == rhs.writer_version;
  }
};

// The channel-specific metadata of the ISO 21496-1 gain map metadata.
struct IsoGainMapChannelMetadata {
  // The channel's gain map min value expressed as a numerator and denominator.
  Int32 gain_map_min_numerator = 0;
  UInt32 gain_map_min_denominator = 1;

  // The channel's gain map max value expressed as a numerator and denominator.
  Int32 gain_map_max_numerator = 0;
  UInt32 gain_map_max_denominator = 1;

  // The channel's gamma value expressed as a numerator and denominator.
  UInt32 gamma_numerator = 1;
  UInt32 gamma_denominator = 1;

  // The channel's base offset value expressed as a numerator and denominator.
  Int32 base_offset_numerator = 0;
  UInt32 base_offset_denominator = 1;

  // The channel's alternate offset value expressed as a numerator and
  // denominator.
  Int32 alternate_offset_numerator = 0;
  UInt32 alternate_offset_denominator = 1;

  // @return The channel's gain map min value as a float.
  float GetGainMapMin() const {
    return gain_map_min_denominator == 0
               ? 0
               : static_cast<float>(gain_map_min_numerator) /
                     static_cast<float>(gain_map_min_denominator);
  }

  // @return The channel's gain map max value as a float.
  float GetGainMapMax() const {
    return gain_map_max_denominator == 0
               ? 0
               : static_cast<float>(gain_map_max_numerator) /
                     static_cast<float>(gain_map_max_denominator);
  }

  // @return The channel's gamma value as a float.
  float GetGamma() const {
    return gamma_denominator == 0 ? 1
                                  : static_cast<float>(gamma_numerator) /
                                        static_cast<float>(gamma_denominator);
  }

  // @return The channel's base offset value as a float.
  float GetBaseOffset() const {
    return base_offset_denominator == 0
               ? 0
               : static_cast<float>(base_offset_numerator) /
                     static_cast<float>(base_offset_denominator);
  }

  // @return The channel's alternate offset value as a float.
  float GetAlternateOffset() const {
    return alternate_offset_denominator == 0
               ? 0
               : static_cast<float>(alternate_offset_numerator) /
                     static_cast<float>(alternate_offset_denominator);
  }

  // @return Whether the channel metadata is valid.
  bool IsValid() const {
    return gain_map_min_denominator > 0 && gain_map_max_denominator > 0 &&
           base_offset_denominator > 0 && alternate_offset_denominator > 0 &&
           gamma_denominator > 0 && GetGainMapMax() >= GetGainMapMin();
  }

  // @return Whether the channel metadata is equal to the rhs channel metadata.
  bool operator==(const IsoGainMapChannelMetadata& rhs) const {
    return gain_map_min_numerator == rhs.gain_map_min_numerator &&
           gain_map_min_denominator == rhs.gain_map_min_denominator &&
           gain_map_max_numerator == rhs.gain_map_max_numerator &&
           gain_map_max_denominator == rhs.gain_map_max_denominator &&
           gamma_numerator == rhs.gamma_numerator &&
           gamma_denominator == rhs.gamma_denominator &&
           base_offset_numerator == rhs.base_offset_numerator &&
           base_offset_denominator == rhs.base_offset_denominator &&
           alternate_offset_numerator == rhs.alternate_offset_numerator &&
           alternate_offset_denominator == rhs.alternate_offset_denominator;
  }
};

// The metadata of the ISO 21496-1 gain map metadata.
struct IsoGainMapMetadata {
  // The version of the ISO 21496-1 gain map metadata.
  IsoGainMapVersion version;

  // Whether the gain map metadata is single channel or multi-channel (RGB)
  bool is_multichannel = false;

  // Whether to use the base color space for the gain map image data.
  bool use_base_color_space = true;

  // The base HDR headroom expressed as a numerator and denominator.
  UInt32 base_hdr_headroom_numerator = 0;
  UInt32 base_hdr_headroom_denominator = 1;

  // The alternate HDR headroom expressed as a numerator and denominator.
  UInt32 alternate_hdr_headroom_numerator = 0;
  UInt32 alternate_hdr_headroom_denominator = 1;

  // The channel-specific metadata of the ISO 21496-1 gain map metadata.
  std::vector<IsoGainMapChannelMetadata> channels;

  // @return The required size of the channel vector.
  size_t GetRequiredChannelVectorSize() const {
    return is_multichannel ? 3 : 1;
  }

  // @return The base HDR headroom as a float.
  float GetBaseHdrHeadroom() const {
    return base_hdr_headroom_denominator == 0
               ? 0
               : static_cast<float>(base_hdr_headroom_numerator) /
                     static_cast<float>(base_hdr_headroom_denominator);
  }

  // @return The alternate HDR headroom as a float.
  float GetAlternateHdrHeadroom() const {
    return alternate_hdr_headroom_denominator == 0
               ? 0
               : static_cast<float>(alternate_hdr_headroom_numerator) /
                     static_cast<float>(alternate_hdr_headroom_denominator);
  }

  // @return Whether the metadata is valid.
  bool IsValid() const {
    return version.IsValid() && base_hdr_headroom_denominator > 0 &&
           alternate_hdr_headroom_denominator > 0 &&
           channels.size() == GetRequiredChannelVectorSize() &&
           std::all_of(channels.begin(), channels.end(),
                       [](const IsoGainMapChannelMetadata& channel) {
                         return channel.IsValid();
                       });
  }

  // @return Whether the metadata is equal to the rhs metadata.
  bool operator==(const IsoGainMapMetadata& rhs) const {
    return version == rhs.version && is_multichannel == rhs.is_multichannel &&
           use_base_color_space == rhs.use_base_color_space &&
           base_hdr_headroom_numerator == rhs.base_hdr_headroom_numerator &&
           base_hdr_headroom_denominator == rhs.base_hdr_headroom_denominator &&
           alternate_hdr_headroom_numerator ==
               rhs.alternate_hdr_headroom_numerator &&
           alternate_hdr_headroom_denominator ==
               rhs.alternate_hdr_headroom_denominator &&
           GetRequiredChannelVectorSize() ==
               rhs.GetRequiredChannelVectorSize() &&
           std::equal(channels.begin(), channels.end(), rhs.channels.begin(),
                      rhs.channels.end());
  }
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_ISO_ISO_GAIN_MAP_METADATA_H_   // NOLINT
