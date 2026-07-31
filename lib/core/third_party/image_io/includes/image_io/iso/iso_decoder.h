#ifndef IMAGE_IO_ISO_ISO_DECODER_H_  // NOLINT
#define IMAGE_IO_ISO_ISO_DECODER_H_  // NOLINT

#include <string>
#include <vector>

#include "image_io/base/data_range.h"
#include "image_io/iso/iso_types.h"

namespace photos_editing_formats {
namespace image_io {

/// A class that decodes various types of values according to the ISO 14496-12
/// (ISO base media file format, fifth edition - 2015-12-15) specification.
/// Section 4 of that specification dictates the use of a big-endian MSB-first
/// encoding). This class also supports the decoding a unsigned integer value of
/// variable number of bytes for an expandable "class" size as defined in the
/// ISO 14496-1 specification (Systems, fourth edition, 2010-06-01),
/// section 8.3.3. This IsoDecoder class refers to that type of encoding as a
/// "UIntX" encoding.
class IsoDecoder {
 public:
  IsoDecoder() : IsoDecoder(nullptr, 0, 0) {}

  /// @param bytes The array of bytes that are to be decoded.
  /// @param begin The index in the array of bytes where decoding starts.
  /// @param end_index One past the last decodable byte in the array.
  IsoDecoder(const UInt8* bytes, size_t begin, size_t end)
      : bytes_(bytes),
        range_(begin, end),
        next_(begin),
        read_past_end_count_(0),
        unexpected_tag_count_(0),
        incomplete_decoding_count_(0),
        is_verbose_(false) {}

  /// Resets the decoder for a new decoding task.
  /// @param bytes The array of bytes that are to be decoded.
  /// @param begin The index in the array of bytes where decoding starts.
  /// @param end_index One past the last decodable byte in the array.
  void Reset(const UInt8* bytes, size_t begin, size_t end);

  /// Returns a new decoder that refers to a subrange of this decoder. The begin
  /// index of the returned slice will always be >= the begin index value of
  /// this decoder, and the end index of the returned slice will always be <=
  /// the end index of this decoder. The next index of the slice will be set
  /// to its begin index, and the ok value will be set to true.
  /// @param begin The begin index of the slice.
  /// @param end The end index of the slice.
  /// @return The new decoder.
  IsoDecoder GetSlice(size_t begin, size_t end) const;

  /// Returns a new decoder that refers to a subrange of this decoder. The begin
  /// and next indices of the returned slice are set to this decoder's
  /// next index and the end index of the returned slice will always be <=
  /// the end index of this decoder.
  /// @param end The end index of the slice.
  /// @return The new decoder.
  IsoDecoder GetSlice(size_t end) const { return GetSlice(next_, end); }

  /// @param slice The decoder previously obtained with the GetSlice() function
  /// the counts and messages of which are to be merged with this decoder.
  void CaptureSliceCountsAndMessages(const IsoDecoder& slice);

  /// @return The array of bytes being decoded.
  const UInt8* GetBytes() const { return bytes_; }

  /// @return The index in the array of bytes where decoding starts.
  size_t GetBegin() const { return range_.GetBegin(); }

  /// @return The index of the next decodable byte.
  size_t GetNext() const { return next_; }

  /// @param delta The amount to increment the next index by.
  void IncrementNext(size_t delta) { next_ += delta; }

  /// @return One past the last decodable byte in the array.
  size_t GetEnd() const { return range_.GetEnd(); }

  /// @param index The new value for the end index data member.
  // void SetEndIndex(size_t index) { end_index_ = index; }

  /// @return The number of bytes being docoded.
  size_t GetLength() const { return range_.GetLength(); }

  /// @return The number of bytes that are still remaining to be decoded.
  size_t GetRemaining() const {
    return GetEnd() >= next_ ? GetEnd() - next_ : 0;
  }

  /// @return Whether verbose messages should be generated or not.
  bool IsVerbose() const { return is_verbose_; }

  /// @param is_verbose Whether verbose messages should be generated or not.
  void SetVerbose(bool is_verbose) { is_verbose_ = is_verbose; }

  /// @return Whether the decoder has errors or not - that is, whether there
  /// have been no attempts to read past the end of the decodable bytes or
  /// whether there have been unexpected tag values (i.e., object types)
  /// encountered.
  bool HasErrors() const {
    return read_past_end_count_ > 0 || unexpected_tag_count_ > 0;
  }

  /// @return The number of attempts that have been made to read past the end of
  /// the decodable bytes. Non-zero values indicate a decoding error.
  UInt32 GetReadPastEndCount() const { return read_past_end_count_; }

  /// @param delta The amount to increment the read past end count by.
  void IncrementReadPastEndCount(UInt32 delta) {
    read_past_end_count_ += delta;
  }

  /// @return The number of unexpected tag values (i.e., object types) that have
  /// been encountered. Non-zero values indicate a decoding error.
  UInt32 GetUnexpectedTagCount() const { return unexpected_tag_count_; }

  /// @param delta The amount to increment the unexpected tag count by.
  void IncrementUnexpectedTagCount(UInt32 delta) {
    unexpected_tag_count_ += delta;
  }

  /// @return The number of unexpected tag values (i.e., object types) that have
  /// been encountered. Non-zero values indicate the byte stream might have
  /// been written using later versions of objects than are beding used to
  /// decode the stream. This is typically not an error condition.
  UInt32 GetIncompleteDecodingCount() const {
    return incomplete_decoding_count_;
  }

  /// @param delta The amount to increment the incomplete decoding count by.
  void IncrementIncompleteDecodingCount(UInt32 delta) {
    incomplete_decoding_count_ += delta;
  }

  /// @param message The message to add to the array of verbose messages (if
  /// the verbose messages flag is set).
  void AddVerboseMessage(const std::string& verbose_message) {
    if (is_verbose_) {
      verbose_messages_.push_back(verbose_message);
    }
  }

  /// @return The verbose messages that have been generated.
  const std::vector<std::string>& GetVerboseMessages() const {
    return verbose_messages_;
  }

  /// @return A UInt8 value decoded from the next decodable byte.
  /// This function sets ok to false if there are not enough decodable bytes.
  UInt8 DecodeUInt8Value();

  /// @return A UInt32 value decoded from the next 4 decodable bytes.
  /// This function sets ok to false if there are not enough decodable bytes.
  UInt32 DecodeUInt32Value();

  /// @return A UInt64 value decoded from the next 8 decodable bytes.
  /// This function sets ok to false if there are not enough decodable bytes.
  UInt64 DecodeUInt64Value();

  /// @return A Int64 value decoded from the next 8 decodable bytes.
  /// This function sets ok to false if there are not enough decodable bytes.
  Int64 DecodeInt64Value();

  /// @return A float value decoded from the next 4 decodable bytes.
  /// This function sets ok to false if there are not enough decodable bytes.
  float DecodeFloatValue();

  /// @return A UIntX value decoded from the next 1 - 5 decodable bytes.
  /// This function sets ok to false if there are not enough decodable bytes.
  UIntX DecodeUIntXValue();

  /// Obtains the next UInt8 value from this decoder, and compares it with the
  /// expected tag value, returning true if they match. If they don't match
  /// increments the unexpected tag count and optionally creates and adds a
  /// verbose message describing the situation.
  /// @param expected_tag The tag value that is expected to be decoded.
  /// @param class_name The name of the subclass associated with the expected
  /// tag value, used to generate the verbose debug message.
  /// @return Whether the decoded tag value matches the expected_tag value.
  bool DecodeTagValue(UInt8 expected_tag, const char* class_name);

 private:
  /// The array of bytes being decoded.
  const UInt8* bytes_;

  /// The range of bytes in the array of bytes where decoding is to be done.
  DataRange range_;

  /// The index of the next decodable byte.
  size_t next_;

  /// Number of attempts to read past end of bytes.
  UInt32 read_past_end_count_;

  /// Number of unexpected tag values encountered.
  UInt32 unexpected_tag_count_;

  /// Number of incomplete_decodings encountered.
  UInt32 incomplete_decoding_count_;

  /// The verbose messages describing unexpected tags and incomplete decodings.
  std::vector<std::string> verbose_messages_;

  /// Whether to create verbose messages as an aid to debugging.
  bool is_verbose_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_ISO_ISO_DECODER_H_  // NOLINT
