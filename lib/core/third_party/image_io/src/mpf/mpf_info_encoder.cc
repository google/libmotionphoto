#include "image_io/mpf/mpf_info_encoder.h"

#include <vector>

#include "image_io/base/types.h"
#include "image_io/mpf/mpf_info.h"
#include "image_io/mpf/mpf_info_constants.h"

namespace photos_editing_formats {
namespace image_io {

size_t MpfInfoEncoder::GetEncodedSize(const MpfInfo& mpf_info,
                                      bool include_app2_prefix) {
  return sizeof(kMpfSig) +                          // Signature
         kMpfEndianSize +                           // Endianness
         sizeof(UInt32) +                           // Index IFD Offset
         sizeof(UInt16) +                           // Tag count
         kMpfTagSerializedCount * kMpfTagSize +     // 3 tags at 12 bytes each
         sizeof(UInt32) +                           // Attribute IFD offset
         mpf_info.entries.size() * kMpfEntrySize +  // MPF Entries for images
         (include_app2_prefix ? kApp2SigSize + kApp2LengthFieldSize : 0);
}

const std::vector<Byte>& MpfInfoEncoder::Encode(const MpfInfo& mpf_info,
                                                bool include_app2_prefix) {
  // Start with an empty byte array and write the signature and endianness.
  // This encoder always uses the big endian format.
  UInt32 app2_prefix_length = 0;
  encoder_.GetMutableBytes().clear();
  if (include_app2_prefix) {
    encoder_.Append(kApp2Sig, kApp2SigSize);
    encoder_.EncodeUInt16Value(sizeof(UInt16) +
                               GetEncodedSize(mpf_info, false));
    app2_prefix_length = kApp2SigSize + kApp2LengthFieldSize;
  }
  encoder_.Append(kMpfSig, kMpfSigSize);
  encoder_.Append(kMpfBigEndian, kMpfEndianSize);

  // Set the Index IFD offset to be after the endianness and this offset.
  UInt32 index_ifd_offset = kMpfEndianSize + sizeof(UInt32);
  encoder_.EncodeUInt32Value(index_ifd_offset);

  // Write 3 tags: version, number of images and MP entries.
  constexpr UInt16 kMpfTagCount = 3;
  encoder_.EncodeUInt16Value(kMpfTagCount);

  // Write the version tag.
  encoder_.EncodeUInt16Value(kMpfVersionTag);
  encoder_.EncodeUInt16Value(kMpfVersionType);
  encoder_.EncodeUInt32Value(kMpfVersionCount);
  encoder_.Append(kMpfVersionExpected, kMpfVersionSize);

  // Write the number of images tag.
  const UInt32 number_of_images = static_cast<UInt32>(mpf_info.entries.size());
  encoder_.EncodeUInt16Value(kMpfNumberOfImagesTag);
  encoder_.EncodeUInt16Value(kMpfNumberOfImagesType);
  encoder_.EncodeUInt32Value(kMpfNumberOfImagesCount);
  encoder_.EncodeUInt32Value(number_of_images);

  // Write the MP entries.
  encoder_.EncodeUInt16Value(kMpfEntryTag);
  encoder_.EncodeUInt16Value(kMpfEntryType);
  encoder_.EncodeUInt32Value(kMpfEntrySize * number_of_images);
  const UInt32 entry_offset = static_cast<UInt32>(
      encoder_.GetBytes().size() -  // The bytes written so far
      kMpfSigSize -                 // Excluding the MPF signature
      app2_prefix_length +          // Excluding the optional APP2 prefix
      sizeof(UInt32) +              // Including this offset
      sizeof(UInt32));              // And including the attribute IFD offset
  encoder_.EncodeUInt32Value(entry_offset);

  // Write the (unused) IFD offset and the MP entries.
  encoder_.EncodeUInt32Value(0);
  for (UInt32 image_index = 0; image_index < number_of_images; ++image_index) {
    const auto& image = mpf_info.entries[image_index];
    UInt32 attribute = image.attribute;
    if (image_index == 0) {
      attribute |= kMpfEntryAttributeTypePrimary;
    } else {
      attribute = attribute & (!kMpfEntryAttributeTypePrimary);
    }
    encoder_.EncodeUInt32Value(attribute);
    encoder_.EncodeUInt32Value(image.image_size);
    encoder_.EncodeUInt32Value(image.image_offset);
    encoder_.EncodeUInt16Value(image.dependent_image1_entry_number);
    encoder_.EncodeUInt16Value(image.dependent_image2_entry_number);
  }
  return encoder_.GetBytes();
}

}  // namespace image_io
}  // namespace photos_editing_formats
