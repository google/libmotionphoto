#ifndef IMAGE_IO_BASE_BYTE_DECODER_H_  // NOLINT
#define IMAGE_IO_BASE_BYTE_DECODER_H_  // NOLINT

#include <cstddef>
#include <string>

#include "image_io/base/message.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/types.h"

namespace photos_editing_formats {
namespace image_io {

// A base class for decoders that scan bytes for strings and numbers.
class ByteArrayDecoder {
 public:
  //   message_handler: An optional message handler used to report error
  // and warning conditions encountered while decoding the bytes.
  explicit ByteArrayDecoder(MessageHandler* message_handler);

  //   bytes: The bytes to examine. The memory pointed to must be valid
  // throughout the lifetime of this decoder.
  //   count: The number of bytes to examine.
  void SetBytesAndCount(const Byte* bytes, size_t count) {
    bytes_ = bytes;
    count_ = count;
  }

  // Returns the message handler.
  MessageHandler* GetMessageHandler() const { return message_handler_; }

  // Returns whether the decoder is set up for big endian values.
  bool IsBigEndian() const { return is_big_endian_; }

  // Returns the address of the start of the byte array.
  const Byte* GetBytes() const { return bytes_; }

  // Returns the size of the byte array.
  size_t GetCount() const { return count_; }

  //   is_big_endian: Whether the integer data is big_endian.
  void SetIsBigEndian(bool is_big_endian) { is_big_endian_ = is_big_endian; }

  //   index: The index of the byte to check.
  //   count: The number of bytes in the block.
  // Returns whether the bytes in the block [index:index+count) can be read.
  bool IsValidRange(size_t index, size_t count) const;

  //   count: The number of bytes to skip.
  //   index: The index at which to start skipping. The count value is
  // added to the index if the skip is valid.
  // Returns whether skipping the bytes is valid.
  bool SkipBytes(size_t count, size_t* index);

  //   index: The index from which to compare the bytes.
  //   byteArray: The bytes to use in the comparison.
  //   count: The number of bytes to conpare.
  // Returns whether the bytes were compared successfully.
  bool CompareBytes(size_t index, const Byte* byteArray, size_t count);

  //   expecting: A description of the expected UInt32 value.
  //   index: The index at which to get the UInt32 value. If a value is
  // obtained successfully, the index is incremented by 4.
  //   value: The UInt32 pointer to receive the value.
  // Returns whether a UInt32 value was successfully obtained.
  bool GetValue(const std::string& expecting, size_t* index, UInt32* value);

  //   expecting: A description of the expected UInt16 value.
  //   index: The index at which to get the UInt16 value. If a value is
  // obtained successfully, the index is incremented by 2.
  //   value: The UInt16 pointer to receive the value.
  // Returns whether a UInt16 value was successfully obtained.
  bool GetValue(const std::string& expecting, size_t* index, UInt16* value);

  //   expecting: A description of the expected UInt8 value.
  //   index: The index at which to get the UInt8 value. If a value is
  // obtained successfully, the index is incremented by 1.
  //   value: The UInt8 pointer to receive the value.
  // Returns whether a UInt8 value was successfully obtained.
  bool GetValue(const std::string& expecting, size_t* index, UInt8* value);

  //   index: The index from which to obtain a byte value
  // Returns a string that contains "value=XX" or "the end of the buffer"
  std::string GetByteValueString(size_t index) const;

  // Reports an error message describing what was expected at the index.
  //   expecting: A description of what was expected.
  //   index: The index at which something was expected.
  // Returns a false value.
  bool ReportExpectedError(const std::string& expecting, size_t index);

  // Reports an error message expected at the index.
  //   message: A description of the error.
  //   index: The index at which the error occurred
  // Returns a false value.
  bool ReportError(const std::string& message, size_t index);

  // Reports an error message not related to parsing at an index.
  //   message: A description of the error.
  // Returns a false value.
  bool ReportError(const std::string& message) {
    return ReportError(message, GetCount());
  }

  //   message: The message to report to the message handler.
  void ReportMessage(const Message& message);

 private:
  MessageHandler* message_handler_;
  const Byte* bytes_;
  size_t count_;
  bool is_big_endian_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_BASE_BYTE_DECODER_H_  // NOLINT
