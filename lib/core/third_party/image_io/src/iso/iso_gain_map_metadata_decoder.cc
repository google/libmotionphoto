#include "image_io/iso/iso_gain_map_metadata_decoder.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#include "image_io/base/byte_array_decoder.h"
#include "image_io/base/jpeg_app2_constants.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/types.h"
#include "image_io/iso/iso_gain_map_constants.h"
#include "image_io/iso/iso_gain_map_metadata.h"

namespace photos_editing_formats {
namespace image_io {

namespace {
/// @param channel The channel number.
/// @return The prefix to use for the channel.
std::string ChannelPrefix(size_t channel) {
  std::stringstream ss;
  ss << "channel_" << channel << ":";
  return ss.str();
}
}  // namespace

IsoGainMapMetadataDecoder::IsoGainMapMetadataDecoder(
    MessageHandler* message_handler)
    : ByteArrayDecoder(message_handler) {}

size_t IsoGainMapMetadataDecoder::SkipApp2Identifier(const Byte* bytes,
                                                     size_t count,
                                                     bool required) {
  SetBytesAndCount(bytes, count);
  bool idPresent = IsValidRange(0, kApp2SigSize + kApp2LengthFieldSize) &&
                   memcmp(bytes, kApp2Sig, sizeof(kApp2Sig)) == 0;
  if (!idPresent && required) {
    ReportExpectedError("APP2 identifier", 0);
  }
  return idPresent ? kApp2SigSize + kApp2LengthFieldSize : 0;
}

size_t IsoGainMapMetadataDecoder::SkipUrnIdentifier(const Byte* bytes,
                                                    size_t count,
                                                    bool required) {
  SetBytesAndCount(bytes, count);
  bool idPresent = IsValidRange(0, sizeof(kIsoGainMapMetadataURN)) &&
                   memcmp(bytes, kIsoGainMapMetadataURN,
                          sizeof(kIsoGainMapMetadataURN)) == 0;
  if (!idPresent && required) {
    ReportExpectedError("URN identifier", 0);
  }
  return idPresent ? sizeof(kIsoGainMapMetadataURN) : 0;
}

bool IsoGainMapMetadataDecoder::Decode(const Byte* bytes, size_t count,
                                       DecodeType decode_type) {
  // Start decoding the metadata version and return early if that is all that
  // is requested.
  SetBytesAndCount(bytes, count);
  size_t index = 0;
  if (!DecodeVersion(&index, &metadata_.version)) {
    return false;
  }
  if (decode_type == kDecodeVersionOnly) {
    return true;
  }

  // Decode the rest of the metadata, starting with the is_multichannel and
  // use_base_color_space fields and then skip the six padding bytes.
  UInt8 flags_byte = 0;
  if (!GetValue("flags_byte", &index, &flags_byte)) {
    return false;
  }
  metadata_.is_multichannel = flags_byte & (1 << 7);
  metadata_.use_base_color_space = flags_byte & (1 << 6);

  // Decode the headroom numerators and denominators.
  if (!DecodeUnsignedNumeratorAndDenominator(
          "base_hdr_headroom", &index, &metadata_.base_hdr_headroom_numerator,
          &metadata_.base_hdr_headroom_denominator)) {
    return false;
  }
  if (!DecodeUnsignedNumeratorAndDenominator(
          "alternate_hdr_headroom", &index,
          &metadata_.alternate_hdr_headroom_numerator,
          &metadata_.alternate_hdr_headroom_denominator)) {
    return false;
  }

  // Decode the channel metadata for each channel.
  size_t channel_count = metadata_.GetRequiredChannelVectorSize();
  for (size_t channel = 0; channel < channel_count; ++channel) {
    std::string channel_prefix = ChannelPrefix(channel);
    IsoGainMapChannelMetadata channel_metadata;
    if (!DecodeChannelMetadata(channel_prefix, &index, &channel_metadata)) {
      return false;
    }
    metadata_.channels.emplace_back(channel_metadata);
  }
  return true;
}

bool IsoGainMapMetadataDecoder::DecodeVersion(size_t* index,
                                              IsoGainMapVersion* version) {
  if (!GetValue("minimum_version", index, &version->minimum_version)) {
    return false;
  }
  if (!GetValue("writer_version", index, &version->writer_version)) {
    return false;
  }
  return true;
}

bool IsoGainMapMetadataDecoder::DecodeChannelMetadata(
    const std::string& channel_prefix, size_t* index,
    IsoGainMapChannelMetadata* channel_metadata) {
  if (!DecodeSignedNumeratorAndDenominator(
          channel_prefix + "gain_map_min", index,
          &channel_metadata->gain_map_min_numerator,
          &channel_metadata->gain_map_min_denominator)) {
    return false;
  }
  if (!DecodeSignedNumeratorAndDenominator(
          channel_prefix + "gain_map_max", index,
          &channel_metadata->gain_map_max_numerator,
          &channel_metadata->gain_map_max_denominator)) {
    return false;
  }
  if (!DecodeUnsignedNumeratorAndDenominator(
          channel_prefix + "gamma", index, &channel_metadata->gamma_numerator,
          &channel_metadata->gamma_denominator)) {
    return false;
  }
  if (!DecodeSignedNumeratorAndDenominator(
          channel_prefix + "base_offset", index,
          &channel_metadata->base_offset_numerator,
          &channel_metadata->base_offset_denominator)) {
    return false;
  }
  if (!DecodeSignedNumeratorAndDenominator(
          channel_prefix + "alternate_offset", index,
          &channel_metadata->alternate_offset_numerator,
          &channel_metadata->alternate_offset_denominator)) {
    return false;
  }
  return true;
}

bool IsoGainMapMetadataDecoder::DecodeSignedNumeratorAndDenominator(
    const std::string& expecting, size_t* index, Int32* numerator,
    UInt32* denominator) {
  UInt32 unsigned_numerator = 0;
  if (!DecodeUnsignedNumeratorAndDenominator(
          expecting, index, &unsigned_numerator, denominator)) {
    return false;
  }
  *numerator = static_cast<Int32>(unsigned_numerator);
  return true;
}

bool IsoGainMapMetadataDecoder::DecodeUnsignedNumeratorAndDenominator(
    const std::string& expecting, size_t* index, UInt32* numerator,
    UInt32* denominator) {
  if (!GetValue(expecting + "_numerator", index, numerator)) {
    return false;
  }
  if (!GetValue(expecting + "_denominator", index, denominator)) {
    return false;
  }
  if (*denominator == 0) {
    ReportError(expecting + " denominator is 0", *index);
    return false;
  }
  return true;
}

}  // namespace image_io
}  // namespace photos_editing_formats
