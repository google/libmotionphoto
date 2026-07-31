#include "image_io/base/byte_array_decoder.h"

#include <cctype>
#include <cstring>
#include <sstream>

#include "image_io/base/byte_data.h"
#include "image_io/base/message.h"
#include "image_io/base/types.h"

namespace photos_editing_formats {
namespace image_io {

using std::string;
using std::stringstream;

namespace {

/// @param bytes The array containing the bytes to shift into an output UInt32.
/// @param index The index in the array containing the byte to shift.
/// @param shift The number of bits to shift the byte.
/// @return The byte[index] << shift value.
UInt32 Shift(const Byte* bytes, size_t index, int shift) {
  return static_cast<UInt32>(bytes[index]) << shift;
}

}  // namespace

ByteArrayDecoder::ByteArrayDecoder(MessageHandler* message_handler)
    : message_handler_(message_handler),
      bytes_(nullptr),
      count_(0),
      is_big_endian_(true) {}

bool ByteArrayDecoder::IsValidRange(size_t index, size_t count) const {
  return count <= count_ && index <= count_ - count;
}

bool ByteArrayDecoder::SkipBytes(size_t count, size_t* index) {
  if (!IsValidRange(*index, count)) {
    stringstream ss;
    ss << "Attempting to skip " << count << "bytes";
    return ReportExpectedError(ss.str(), *index);
  }
  *index += count;
  return true;
}

bool ByteArrayDecoder::CompareBytes(size_t index, const Byte* byteArray,
                                    size_t count) {
  if (!IsValidRange(index, count)) {
    return false;
  }
  return memcmp(bytes_ + index, byteArray, count) == 0;
}

bool ByteArrayDecoder::GetValue(const string& expecting, size_t* index,
                                UInt32* value) {
  constexpr size_t kLen = 4;
  if (!IsValidRange(*index, kLen)) {
    return ReportExpectedError(expecting, *index);
  }
  if (is_big_endian_) {
    *value = Shift(bytes_, *index, 24) | Shift(bytes_, *index + 1, 16) |
             Shift(bytes_, *index + 2, 8) | bytes_[*index + 3];
  } else {
    *value = Shift(bytes_, *index + 3, 24) | Shift(bytes_, *index + 2, 16) |
             Shift(bytes_, *index + 1, 8) | bytes_[*index];
  }
  *index += kLen;
  return true;
}

bool ByteArrayDecoder::GetValue(const string& expecting, size_t* index,
                                UInt16* value) {
  constexpr size_t kLen = 2;
  if (!IsValidRange(*index, kLen)) {
    return ReportExpectedError(expecting, *index);
  }
  if (is_big_endian_) {
    *value = Shift(bytes_, *index, 8) | bytes_[*index + 1];
  } else {
    *value = Shift(bytes_, *index + 1, 8) | bytes_[*index];
  }
  *index += kLen;
  return true;
}

bool ByteArrayDecoder::GetValue(const string& expecting, size_t* index,
                                UInt8* value) {
  constexpr size_t kLen = 1;
  if (!IsValidRange(*index, kLen)) {
    return ReportExpectedError(expecting, *index);
  }
  *value = bytes_[*index];
  *index += kLen;
  return true;
}

string ByteArrayDecoder::GetByteValueString(size_t index) const {
  stringstream ss;
  if (index < count_) {
    const char cbyte = static_cast<const char>(bytes_[index]);
    ss << "value="
       << "0x" << ByteData::Byte2Hex(bytes_[index]);
    if (isprint(cbyte)) ss << " ('" << cbyte << "')";
  } else {
    ss << "the end of the buffer";
  }
  return ss.str();
}

bool ByteArrayDecoder::ReportExpectedError(const string& expecting,
                                           size_t index) {
  stringstream ss;
  ss << "Expected " << expecting << " at index=" << index << ": instead found "
     << GetByteValueString(index);
  ReportMessage(Message(Message::kDecodingError, 0, ss.str()));
  return false;
}

bool ByteArrayDecoder::ReportError(const string& message, size_t index) {
  stringstream ss;
  ss << message << " at index=" << index;
  ReportMessage(Message(Message::kDecodingError, 0, ss.str()));
  return false;
}

void ByteArrayDecoder::ReportMessage(const Message& message) {
  if (message_handler_ != nullptr) {
    message_handler_->ReportMessage(message);
  }
}

}  // namespace image_io
}  // namespace photos_editing_formats
