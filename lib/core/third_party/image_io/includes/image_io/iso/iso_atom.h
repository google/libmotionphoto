#ifndef IMAGE_IO_ISO_ISO_ATOM_H_  // NOLINT
#define IMAGE_IO_ISO_ISO_ATOM_H_  // NOLINT

#include <functional>
#include <string>
#include <vector>

#include "image_io/base/data_source.h"

namespace photos_editing_formats {
namespace image_io {

/// A bare bones representation of an Atom like those found in mp4/heic files.
///
/// The basic structure of an atom in a data stream is either:
///   Standard length format: [length4][type4][payloadN]
///   Extended length format: [1][type4][length8][payloadN]
/// Where length4 is a 4 byte unsigned big endian integer, type4 is four bytes
/// of character data that represents the type, and length8 is an 8 byte
/// unsigned big endian integer, and payloadN is some number of payload bytes.
///
/// The number of bytes represented payloadN for the standard length bytes is
///    length4 - 8 if length4 > 1 or to the end of file if length4 is 0.
///    length8 - 16 if length4 == 1.
/// If length4 > 1 then it must also be >=8.
/// If length4 == 1 then length8 must be >= 16.
///
/// All that being said, IsoAtom does not let you reprsent an invalid atom based
/// on the header/payload length values. But you must specify a valid 4 ch
class IsoAtom {
 public:
  /// A typedef for the Scan function below. The function to receive the results
  /// of scanning a valid atom. The atom's start index is passed as the first
  /// parameter, followed by the atom just scanned. The function should return
  /// then next index to continue scanning at (usually start+atom.GetLength())
  /// or the start index to halt the scan process.
  using ScanReceiver = std::function<UInt64(UInt64, const IsoAtom&)>;

  /// The status codes of a Decode() operation.
  static constexpr int kDecodeOk = 1;
  static constexpr int kDecodeEndOfData = 0;
  static constexpr int kDecodeErrorInvalidDataSource = -1;
  static constexpr int kDecodeErrorPrematureEndOfData = -2;
  static constexpr int kDecodeErrorInvalidAtomLength = -3;
  static constexpr int kDecodeErrorInvalidAtomType = -4;
  static constexpr int kDecodeErrorInvalidScanReceiver = -5;

  /// The type and standard and extended lengths of the Atom's header.
  static constexpr UInt32 kTypeLength = 4;
  static constexpr UInt32 kTypeOffset = 4;
  static constexpr UInt32 kStandardHeaderLength = 8;
  static constexpr UInt32 kStandardHeaderOffset = 0;
  static constexpr UInt32 kExtendedHeaderLength = 16;
  static constexpr UInt32 kExtendedHeaderOffset = 8;

  /// @param payload_length The length of the atom's payload.
  /// @return The min value that can be used for the atom's header length.
  static UInt32 GetMinHeaderLength(UInt64 payload_length);

  /// Scans for atoms starting at the given index. This scan/decoding operation
  /// does not check if there are enough bytes for the atom's payload. Client
  /// code can check in the scan_receiver function to verify if the number of
  /// bytes remaining is enough to hold the payload. If it ignores the issue,
  /// and there are not enough bytes, the scan process will return a non-error
  /// kDecodeEndOfData on the next attempt to read an atom.
  /// @param bytes The bytes to decode
  /// @param start The index at which to start decoding.
  /// @param scan_receiver The function to receive the results of the scan.
  /// @return The status of the last decode operation.
  static int Scan(const std::vector<Byte>& bytes, UInt64 start,
                  ScanReceiver scan_receiver);

  /// Scans for atoms starting at the given index. This scan/decoding operation
  /// does not check if there are enough bytes for the atom's payload. Client
  /// code can check in the scan_receiver function to verify if the number of
  /// bytes remaining is enough to hold the payload. If it ignores the issue,
  /// and there are not enough bytes, the scan process will return a non-error
  /// kDecodeEndOfData on the next attempt to read an atom.
  /// @param num_bytes The number of bytes in the buffer.
  /// @param start The index at which to start decoding.
  /// @param scan_receiver The function to receive the results of the scan.
  /// @return The status of the last decode operation.
  static int Scan(const Byte* bytes, UInt64 num_bytes, UInt64 start,
                  ScanReceiver scan_receiver);

  /// Scans for atoms starting at the given index. This scan/decoding operation
  /// does not check if there are enough bytes for the atom's payload. Client
  /// code can check in the scan_receiver function to verify if the number of
  /// bytes remaining is enough to hold the payload. If it ignores the issue,
  /// and there are not enough bytes, the scan process will return a non-error
  /// kDecodeEndOfData on the next attempt to read an atom.
  /// @param start The index at which to start decoding.
  /// @param scan_receiver The function to receive the results of the scan.
  /// @return The status of the last decode operation.
  static int Scan(DataSource* data_source, UInt64 start,
                  ScanReceiver scan_receiver);

  /// Virtual destructor for subcasses.
  virtual ~IsoAtom() = default;

  /// The default constructor produces an IsValid() == false IsoAtom.
  IsoAtom() : IsoAtom(0, 0, "") {}

  /// @param header_length The length of the header. Should be either
  /// kStandardheaderLength or kExtendedHeaderLength.
  /// @param type The type of atom. It should have length kTypeLength.
  IsoAtom(UInt32 header_length, UInt64 payload_length, const std::string& type)
      : type_(type),
        payload_length_(payload_length),
        header_length_(header_length) {}

  /// @return The type value of the atom.
  std::string GetType() const { return type_; }

  /// @return The header length of the atom.
  UInt32 GetHeaderLength() const { return header_length_; }

  /// @return The payload length of the atom.
  UInt64 GetPayloadLength() const { return payload_length_; }

  /// @return The total length of the atom. Note that this value may be
  /// misleading if IsPayloadSizeToEnd() is true.
  UInt64 GetLength() const {
    return IsValid() ? header_length_ + payload_length_ : 0;
  }

  /// @return Whether the atom is valid - i.e., the type's length is kTypeLength
  /// and the header and payload lengths are correct.
  virtual bool IsValid() const;

  /// @return Whether the payload of the atom extends to the end of the file.
  bool IsPayloadSizeToEnd() const {
    return IsValid() && header_length_ == kStandardHeaderLength &&
           payload_length_ == 0;
  }

  /// The equality operators.
  bool operator!=(const IsoAtom& rhs) const { return !((*this) == rhs); }
  bool operator==(const IsoAtom& rhs) const {
    return type_ == rhs.type_ && header_length_ == rhs.header_length_ &&
           payload_length_ == rhs.payload_length_;
  }

  /// Reduces the header length from extended to standard if possible.
  virtual void ReduceHeaderLengthIfPossible();

  /// Decodes the next atom header at the given index. This decoding operation
  /// does not check if there are enough bytes for the atom's payload.
  /// @param bytes The bytes to decode
  /// @param start The index at which to start decoding.
  /// @return The status of the decode operation.
  int Decode(const std::vector<Byte>& bytes, UInt64 start);

  /// Decodes the next atom header at the given index. This decoding operation
  /// does not check if there are enough bytes for the atom's payload.
  /// @param bytes The byte buffer to decode
  /// @param num_bytes The number of bytes in the buffer.
  /// @param start The index at which to start decoding.
  /// @return The status of the decode operation.
  int Decode(const Byte* bytes, UInt64 num_bytes, UInt64 start);

  /// Decodes the next atom header at the given index. This decoding operation
  /// does not check if there are enough bytes for the atom's payload.
  /// @param data_source The data source containing the bytes to decode.
  /// @param start The index at which to start decoding.
  /// @return The status of the decode operation.
  int Decode(DataSource* data_source, UInt64 start);

  /// @return The encoded bytes of the atom.
  std::vector<Byte> Encode() const;

 protected:
  virtual void clear();
  virtual int DecodeWorker(DataSource* data_source, UInt64 start);
  virtual std::vector<Byte> EncodeWorker() const;

  std::string type_;
  UInt64 payload_length_;
  UInt32 header_length_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_ISO_ISO_ATOM_H_  // NOLINT
