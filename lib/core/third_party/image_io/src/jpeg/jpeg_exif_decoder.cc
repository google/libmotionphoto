#include "image_io/jpeg/jpeg_exif_decoder.h"

#include <cctype>
#include <cstring>
#include <sstream>

#include "image_io/base/byte_array_decoder.h"
#include "image_io/base/image_metadata.h"
#include "image_io/base/types.h"
#include "image_io/jpeg/jpeg_marker.h"

namespace photos_editing_formats {
namespace image_io {

using std::memcmp;
using std::string;
using std::stringstream;

namespace {

/// The codes for the type values in an IFD entry supported by this decoder.
constexpr UInt16 kShortType = 0x0003;
constexpr UInt16 kLongType = 0x0004;  // NOLINT

/// The codes for the tag values in an IFD entry supported by this decoder.
constexpr UInt16 kImageWidthTag = 0x0100;
constexpr UInt16 kImageHeightTag = 0x0101;
constexpr UInt16 kOrientationTag = 0x0112;
constexpr UInt16 kResolutionXTag = 0xA002;
constexpr UInt16 kResolutionYTag = 0xA003;
constexpr UInt16 kExifIfdTag = 0x8769;

UInt32 Shift(const Byte* bytes, size_t index, int shift) {
  return static_cast<UInt32>(bytes[index]) << shift;
}

}  // namespace

JpegExifDecoder::JpegExifDecoder(ImageMetadata* image_metadata,
                                 MessageHandler* message_handler)
    : ByteArrayDecoder(message_handler), image_metadata_(image_metadata) {}

/// @param index The in/out index into the bytes_ buffer, updated to the next
/// available byte index if the function returns true.
/// @param value The ifd index value obtained.
/// @return Whether the operation succeeded. If not, an error is reported.
bool JpegExifDecoder::GetIfdIndex(size_t* index, UInt32* value) {
  return GetValue("Expecting an IFD index", index, value);
}

/// @param index The in/out index into the bytes_ buffer, updated to the next
/// available byte index if the function returns true.
/// @param count The ifd entry count value obtained.
/// @return Whether the operation succeeded. If not, an error is reported.
bool JpegExifDecoder::GetIfdEntryCount(size_t* index, UInt16* count) {
  return GetValue("Expecting an IFD entry count", index, count);
}

/// @param index The in/out index into the bytes_ buffer, updated to the next
/// available byte index if the function returns true.
/// @param tag The tag code for the entry
/// @param type The value type code for the entry
/// @param count The value count value for the entry.
/// @return Whether the operation succeeded. If not, an error is reported.
bool JpegExifDecoder::GetIfdEntry(size_t* index, UInt16* tag, UInt16* type,
                                  UInt32* count) {
  if (!GetValue("Expecting an IFD entry tag", index, tag)) {
    return false;
  }
  if (!GetValue("Expecting an IFD entry value type", index, type)) {
    return false;
  }
  if (!GetValue("Expecting an IFD entry value count", index, count)) {
    return false;
  }
  return true;
}

/// Obtains the IFD entry's value field as an UInt32 number.
/// @param type The value type code for the entry
/// @param index The in/out index into the bytes_ buffer, updated to the next
/// available byte index if the function returns true.
/// @param value The entry's value
/// @return Whether the operation succeeded. If not, an error is reported.
bool JpegExifDecoder::GetIfdEntryNumericValue(UInt16 type, size_t* index,
                                              UInt32* value) {
  const string kExpecting("Expecting an IFD entry value");
  if (type == kShortType) {
    UInt16 short_value;
    bool r1 = GetValue(kExpecting, index, &short_value);
    bool r2 = SkipBytes(2, index);
    *value = short_value;
    return r1 && r2;
  }
  return GetValue(kExpecting, index, value);
}

/// Skips the optional bytes at the start of a JPEG segment.
/// @param index The in/out index into the bytes_ buffer, updated to the next
/// available byte index if the function returns true.
void JpegExifDecoder::SkipOptionalJpegSegmentStuff(size_t* index) {
  const Byte kMarker[] = {JpegMarker::kStart, JpegMarker::kAPP1};
  const Byte kExif[] = {'E', 'x', 'i', 'f', 0, 0};
  const size_t kMarkerLen = sizeof(kMarker);
  const size_t kSizeLen = 2;
  const size_t kExifLen = sizeof(kExif);
  const size_t kStuffLen = kMarkerLen + kSizeLen + kExifLen;
  const Byte* bytes = GetBytes();
  if (IsValidRange(*index, kStuffLen) &&
      memcmp(&bytes[*index], kMarker, kMarkerLen) == 0 &&
      memcmp(&bytes[*index + kMarkerLen + kSizeLen], kExif, kExifLen) == 0) {
    *index += kStuffLen;
  }
}

/// Gets the endian value from the bytes_ buffer and sets the flag.
/// @param index The in/out index into the bytes_ buffer, updated to the next
/// available byte index if the function returns true.
/// @return Whether the operation succeeded. If not, an error is reported.
bool JpegExifDecoder::GetEndian(size_t* index) {
  const size_t kLen = 2;
  const Byte kLittle[kLen] = {'I', 'I'};  // FYI: Intel
  const Byte kBig[kLen] = {'M', 'M'};     // FYI: Motorola
  size_t old_index = *index;
  const Byte* bytes = GetBytes();
  if (IsValidRange(*index, kLen)) {
    if (memcmp(bytes + *index, kLittle, kLen) == 0) {
      SetIsBigEndian(false);
      *index += kLen;
    } else if (memcmp(bytes + *index, kBig, kLen) == 0) {
      SetIsBigEndian(true);
      *index += kLen;
    }
  }
  if (*index == old_index) {
    return ReportError("Expected endian marker (II or MM)", *index);
  }
  return true;
}

/// Gets the entry's value and determines if the value is a legal orientation
/// value. If so, it sets the value in the image metadata. If not it issues a
/// warning about ignoring the value.
/// @param index The in/out index into the bytes_ buffer, updated to the next
/// available byte index if the function returns true.
/// @return Whether the operation succeeded. If not, an error is reported.
bool JpegExifDecoder::SetOrientation(UInt16 type, size_t* index) {
  size_t old_index = *index;
  UInt32 value;
  if (!GetIfdEntryNumericValue(type, index, &value)) {
    return false;
  }
  if (IsLegalOrientation(value)) {
    auto orientation = static_cast<Orientation>(value);
    image_metadata_->SetOrientation(orientation);
  } else {
    stringstream ss;
    ss << "Ignoring illegal orientation value=" << value
       << " at index=" << old_index;
    ReportMessage(Message(Message::kWarning, 0, ss.str()));
  }
  return true;
}

bool JpegExifDecoder::Decode(const std::vector<Byte>& bytes) {
  return Decode(bytes.data(), bytes.size());
}

bool JpegExifDecoder::Decode(const Byte* bytes, size_t count) {
  SetBytesAndCount(bytes, count);
  size_t index = 0;
  SkipOptionalJpegSegmentStuff(&index);
  const size_t exif_base = index;
  if (!GetEndian(&index)) {
    return false;
  }
  if (!SkipBytes(2, &index)) {  // magic number 42
    return false;
  }
  UInt32 next_ifd_index;
  UInt16 ifd_entry_count;
  UInt32 last_ifd_index = 0;
  if (!GetIfdIndex(&index, &next_ifd_index)) {
    return false;
  }
  int error_count = 0;
  while (next_ifd_index > 0 && error_count == 0) {
    if (next_ifd_index >= GetCount() ||
        exif_base > GetCount() - next_ifd_index) {
      ReportError("Invalid IFD offset", exif_base);
      error_count++;
      break;
    }
    index = exif_base + next_ifd_index;
    if (next_ifd_index <= last_ifd_index) {
      ReportError("IFD already processed", index);
      error_count++;
      break;
    }
    last_ifd_index = next_ifd_index;
    next_ifd_index = 0;
    if (!GetIfdEntryCount(&index, &ifd_entry_count)) {
      return false;
    }
    UInt16 tag, type;
    UInt32 count, value;
    for (UInt16 i = 0; i < ifd_entry_count && error_count == 0; ++i) {
      if (!GetIfdEntry(&index, &tag, &type, &count)) {
        error_count++;
        break;
      }
      switch (tag) {
        case kExifIfdTag:
          if (!GetIfdEntryNumericValue(type, &index, &next_ifd_index)) {
            error_count++;
          }
          break;
        case kImageWidthTag:
        case kResolutionXTag:
          if (GetIfdEntryNumericValue(type, &index, &value)) {
            image_metadata_->SetWidth(value);
          } else {
            error_count++;
          }
          break;
        case kImageHeightTag:
        case kResolutionYTag:
          if (GetIfdEntryNumericValue(type, &index, &value)) {
            image_metadata_->SetHeight(value);
          } else {
            error_count++;
          }
          break;
        case kOrientationTag:
          if (!SetOrientation(type, &index)) {
            error_count++;
          }
          break;
        default:
          if (!GetIfdEntryNumericValue(type, &index, &value)) {
            error_count++;
          }
          break;
      }
    }
  }
  return error_count == 0;
}

}  // namespace image_io
}  // namespace photos_editing_formats
