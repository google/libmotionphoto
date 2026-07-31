#include "image_io/iso/iso_gain_map_metadata_encoder.h"

#include <cstddef>
#include <vector>

#include "image_io/base/byte_array_encoder.h"
#include "image_io/base/jpeg_app2_constants.h"
#include "image_io/base/types.h"
#include "image_io/iso/iso_gain_map_constants.h"
#include "image_io/iso/iso_gain_map_metadata.h"

namespace photos_editing_formats {
namespace image_io {

size_t IsoGainMapMetadataEncoder::GetEncodedSize(
    const IsoGainMapMetadata& metadata, PrefixType prefix_type,
    EncodeType encode_type) {
  if ((encode_type == kEncodeVersionOnly && !metadata.version.IsValid()) ||
      (encode_type == kEncodeAll && !metadata.IsValid())) {
    return 0;
  }
  size_t size = 0;
  switch (encode_type) {
    case kEncodeAll:
      size += kIsoGainMapMainMetadataEncodedSize;
      size += (metadata.channels.size() * kIsoGainMapChannelEncodedSize);
      [[fallthrough]];
    case kEncodeVersionOnly:
      size += kIsoGainMapVersionEncodedSize;
      break;
  }
  switch (prefix_type) {
    case kApp2AndUrnPrefix:
      size += (kApp2SigSize + kApp2LengthFieldSize);
      [[fallthrough]];
    case kUrnPrefix:
      size +=
          sizeof(kIsoGainMapMetadataURN);  // sizeof includes the final null.
      break;
    case kNoPrefix:
      break;
  }
  return size;
}

const std::vector<Byte>& IsoGainMapMetadataEncoder::Encode(
    const IsoGainMapMetadata& metadata, PrefixType prefix_type,
    EncodeType encode_type) {
  // Start with an empty encoder and check the validity of the metadata.
  encoder_ = ByteArrayEncoder();
  if ((encode_type == kEncodeVersionOnly && !metadata.version.IsValid()) ||
      (encode_type == kEncodeAll && !metadata.IsValid())) {
    return encoder_.GetBytes();
  }

  // Append the APP2 and URN prefixes if needed.
  switch (prefix_type) {
    case kApp2AndUrnPrefix:
      encoder_.Append(kApp2Sig, kApp2SigSize);
      encoder_.EncodeUInt16Value(
          kApp2LengthFieldSize +
          GetEncodedSize(metadata, kUrnPrefix, encode_type));
      [[fallthrough]];
    case kUrnPrefix:
      encoder_.Append(reinterpret_cast<const Byte*>(kIsoGainMapMetadataURN),
                      sizeof(kIsoGainMapMetadataURN));
      break;
    case kNoPrefix:
      break;
  }

  // Encode the version.
  encoder_.EncodeUInt16Value(metadata.version.minimum_version);
  encoder_.EncodeUInt16Value(metadata.version.writer_version);

  // If not encoding all, just return the bytes.
  if (encode_type == kEncodeVersionOnly) {
    return encoder_.GetBytes();
  }

  // Encode the non-channel part of the metadata.
  UInt8 flags = 0;
  if (metadata.is_multichannel) {
    flags |= (1 << 7);
  }
  if (metadata.use_base_color_space) {
    flags |= (1 << 6);
  }
  encoder_.EncodeUInt8Value(flags);
  encoder_.EncodeUInt32Value(metadata.base_hdr_headroom_numerator);
  encoder_.EncodeUInt32Value(metadata.base_hdr_headroom_denominator);
  encoder_.EncodeUInt32Value(metadata.alternate_hdr_headroom_numerator);
  encoder_.EncodeUInt32Value(metadata.alternate_hdr_headroom_denominator);

  // Encode the channel metadata.
  for (const auto& channel : metadata.channels) {
    encoder_.EncodeUInt32Value(
        static_cast<UInt32>(channel.gain_map_min_numerator));
    encoder_.EncodeUInt32Value(channel.gain_map_min_denominator);
    encoder_.EncodeUInt32Value(
        static_cast<UInt32>(channel.gain_map_max_numerator));
    encoder_.EncodeUInt32Value(channel.gain_map_max_denominator);
    encoder_.EncodeUInt32Value(channel.gamma_numerator);
    encoder_.EncodeUInt32Value(channel.gamma_denominator);
    encoder_.EncodeUInt32Value(
        static_cast<UInt32>(channel.base_offset_numerator));
    encoder_.EncodeUInt32Value(channel.base_offset_denominator);
    encoder_.EncodeUInt32Value(
        static_cast<UInt32>(channel.alternate_offset_numerator));
    encoder_.EncodeUInt32Value(channel.alternate_offset_denominator);
  }

  // And return the bytes.
  return encoder_.GetBytes();
}

}  // namespace image_io
}  // namespace photos_editing_formats
