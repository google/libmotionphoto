#include "image_io/mpf/mpf_info_decoder.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

#include "image_io/base/types.h"
#include "image_io/mpf/mpf_info.h"
#include "image_io/mpf/mpf_info_constants.h"

namespace photos_editing_formats {
namespace image_io {

// Helper macro to define a variable of the given type and get its value.
#define DEFINE_AND_READ(TYPE, VAR, EXPECTING) \
  TYPE VAR = 0;                               \
  size_t VAR##Index = index;                  \
  if (!GetValue(EXPECTING, &index, &VAR)) {   \
    return false;                             \
  }                                           \
  (void)VAR;                                  \
  (void)VAR##Index;

size_t MpfInfoDecoder::SkipApp2Identifier(const Byte* bytes, size_t count,
                                          bool required) {
  SetBytesAndCount(bytes, count);
  const bool id_present =
      IsValidRange(0, kApp2SigSize + kApp2LengthFieldSize) &&
      memcmp(bytes, kApp2Sig, sizeof(kApp2Sig)) == 0;
  if (!id_present && required) {
    ReportExpectedError("APP2 identifier", 0);
  }
  return id_present ? kApp2SigSize + kApp2LengthFieldSize : 0;
}

size_t MpfInfoDecoder::SkipMpfIdentifier(const Byte* bytes, size_t count,
                                         bool required) {
  SetBytesAndCount(bytes, count);
  const bool id_present = IsValidRange(0, kMpfSigSize) &&
                          memcmp(bytes, kMpfSig, sizeof(kMpfSig)) == 0;
  if (!id_present && required) {
    ReportExpectedError("MPF identifier", 0);
  }
  return id_present ? kMpfSigSize : 0;
}

bool MpfInfoDecoder::Decode(const Byte* bytes, size_t count) {
  mpf_info_ = MpfInfo();
  SetBytesAndCount(bytes, count);
  size_t baseIndex = 0;

  // Look for the endian value.
  size_t index = baseIndex;  // kMpfSigSize;
  if (CompareBytes(index, kMpfBigEndian, kMpfEndianSize)) {
    SetIsBigEndian(true);
  } else if (CompareBytes(index, kMpfLittleEndian, kMpfEndianSize)) {
    SetIsBigEndian(false);
  } else {
    ReportExpectedError("Endian marker", index);
    return false;
  }
  index += kMpfEndianSize;

  // Seek to the Index Image File Directory (Index IFD).
  DEFINE_AND_READ(UInt32, indexIfdOffset, "Index IFD offset");
  if (indexIfdOffset >= count) {
    ReportError("Illegal IFD offset value", indexIfdOffsetIndex);
    return false;
  }
  index = baseIndex + indexIfdOffset;

  // Read the number of tags in the Index IFD. See Table 3 (MP Index IFD Tags)
  // for a description of all possible tags.
  DEFINE_AND_READ(UInt16, tagCount, "Tag count");

  // We will extract the number of images from the tags.
  UInt32 numberOfImages = 0;

  // The offset to the MP entries. Zero is an invalid value.
  UInt32 mpEntryOffset = 0;

  // The MP Index IFD tags shall be specified in the order of their tag IDs
  // (text from section 5.2.3), so keep track of the previous tag id read.
  UInt16 previousTagId = 0;
  for (UInt16 tagIndex = 0; tagIndex < tagCount; ++tagIndex) {
    DEFINE_AND_READ(UInt16, tagId, "Tag ID");
    DEFINE_AND_READ(UInt16, type, "Tag Type");
    DEFINE_AND_READ(UInt32, count, "Tag Count");
    DEFINE_AND_READ(UInt32, value, "Tag Value");

    if (previousTagId >= tagId) {
      return ReportError("MPF tags not in order", tagIdIndex);
    }
    previousTagId = tagId;

    switch (tagId) {
      case kMpfVersionTag:
        // See 5.2.3.1: MP Format Version.
        if (!CompareBytes(valueIndex, kMpfVersionExpected, kMpfVersionSize)) {
          return ReportError("Version value is not 0100", valueIndex);
        }
        if (count != kMpfVersionCount) {
          return ReportError("Version count not 4", countIndex);
        }
        break;
      case kMpfNumberOfImagesTag:
        // See 5.2.3.2: Number of Images.
        numberOfImages = value;
        if (type != kMpfTypeLong) {
          return ReportError("Invalid Number of Images type", typeIndex);
        }
        if (numberOfImages < 1) {
          return ReportError("Invalid Number of Images value", valueIndex);
        }
        break;
      case kMpfEntryTag: {
        // See 5.2.3.3: MP Entry.
        if (count != kMpfEntrySize * numberOfImages) {
          return ReportError("Invalid MPEntry count", countIndex);
        }
        mpEntryOffset = value;
        break;
      }
      case kMpfIndividualImageUniqueIDTag:
        // See 5.2.3.4: Individual Image Unique ID List.
        // Validate that the count parameter is correct, but do not extract any
        // other information.
        if (count != kMpfIndividualImageUniqueIDSize * numberOfImages) {
          return ReportError("Invalid Image Unique ID count", countIndex);
        }
        break;
      case kMpfTotalNumberCapturedFramesTag:
        // See 5.2.3.5: Total Number of Captured Frames.
        if (type != kMpfTypeLong) {
          return ReportError("Invalid Total Number of Captured Frames type",
                             typeIndex);
        }
        if (count != kMpfTotalNumberCaptureFramesCount) {
          return ReportError("Invalid Total Number of Captured Frames count",
                             countIndex);
        }
        break;
      default:
        return ReportError("Invalid tag ID", tagIdIndex);
    }
  }
  if (!numberOfImages) {
    return ReportError("Number of images must be greater than zero");
  }
  if (!mpEntryOffset) {
    return ReportError("MP Entry tag was not present or had invalid offset");
  }
  if (mpEntryOffset >= GetCount() || baseIndex > GetCount() - mpEntryOffset) {
    return ReportError("Invalid MP Entry offset", baseIndex);
  }

  // Read the Attribute IFD offset and ignore it. We will not read or validate
  // the Attribute IFD.
  DEFINE_AND_READ(UInt32, attributeIfdOffset, "Attribute IFD offset");

  // Read the MP Entries starting at the offset that we read earlier.
  mpf_info_.entries.resize(numberOfImages);
  index = baseIndex + mpEntryOffset;
  for (UInt32 i = 0; i < numberOfImages; ++i) {
    DEFINE_AND_READ(UInt32, attribute, "Image attribute");
    const bool isPrimary = (attribute & kMpfEntryAttributeTypeMask) ==
                           kMpfEntryAttributeTypePrimary;
    const bool isJpeg = (attribute & kMpfEntryAttributeFormatMask) ==
                        kMpfEntryAttributeFormatJpeg;

    if (isPrimary != (i == 0)) {
      return ReportError("Image must be primary iff it is the first image");
    }
    if (!isJpeg) {
      return ReportError("Image format must be 0 (JPEG)");
    }

    DEFINE_AND_READ(UInt32, size, "Image size");
    DEFINE_AND_READ(UInt32, dataOffset, "Image data offset");
    if (i == 0 && dataOffset != 0) {
      return ReportError("First individual Image offset must be NULL");
    }

    DEFINE_AND_READ(UInt16, dependentImage1EntryNumber,
                    "Dependent image1 entry number");
    DEFINE_AND_READ(UInt16, dependentImage2EntryNumber,
                    "Dependent image2 entry number");
    mpf_info_.entries[i] = {
        .attribute = attribute,
        .image_size = size,
        .image_offset = dataOffset,
        .dependent_image1_entry_number = dependentImage1EntryNumber,
        .dependent_image2_entry_number = dependentImage2EntryNumber,
    };
  }
  mpf_info_.is_valid = true;
  return true;
}

#undef DEFINE_AND_READ

}  // namespace image_io
}  // namespace photos_editing_formats
