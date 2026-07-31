#include "image_io/iso/iso_atom.h"

#include <limits>
#include <string>
#include <utility>

#include "image_io/base/byte_pointer_data_destination.h"
#include "image_io/base/data_segment_data_source.h"

namespace photos_editing_formats {
namespace image_io {

namespace {

/// The offset from the header start to the type and extended length data and
/// the special value that indicates the extended header is present.
const UInt64 kTypeOffset = 4;
const UInt64 kExtendedHeaderLengthOffset = 8;
const UInt64 kExtendedHeaderLengthIndicator = 1;

/// The max payload size for use with a standard header.
constexpr UInt64 kMaxPayloadForStandardHeader =
    std::numeric_limits<UInt32>().max() - IsoAtom::kStandardHeaderLength;
constexpr UInt64 kMaxPayloadForExtendedHeader =
    std::numeric_limits<UInt64>().max() - IsoAtom::kExtendedHeaderLength;

/// @param buffer The buffer from which a UInt64 value is to be read. It is
/// assumed that the buffer has a valid 8 bytes to read from the beginning.
/// @return The value read from the buffer.
UInt64 ReadUInt64BigEndian(const Byte* buffer) {
  return (static_cast<UInt64>(buffer[7]) << 0) |
         (static_cast<UInt64>(buffer[6]) << 8) |
         (static_cast<UInt64>(buffer[5]) << 16) |
         (static_cast<UInt64>(buffer[4]) << 24) |
         (static_cast<UInt64>(buffer[3]) << 32) |
         (static_cast<UInt64>(buffer[2]) << 40) |
         (static_cast<UInt64>(buffer[1]) << 48) |
         (static_cast<UInt64>(buffer[0]) << 56);
}

/// @param value The value to write to the buffer.
/// @param buffer The buffer to receive the bytes written. It is assumed that
/// the buffer is large enough to hold the 8 bytes that are written.
void WriteUInt64BigEndian(Byte* buffer, UInt64 value) {
  for (int i = 7; i >= 0; i--) {
    *buffer++ = ((value >> (8 * i)) & 0xff);
  }
}

/// @param buffer The buffer from which a UInt32 value is to be read. It is
/// assumed that the buffer has a valid 4 bytes to read from the beginning.
/// @return The value read from the buffer.
UInt32 ReadUInt32BigEndian(const Byte* buffer) {
  return (buffer[3] << 0) | (buffer[2] << 8) | (buffer[1] << 16) |
         (buffer[0] << 24);
}

/// @param value The value to write to the buffer.
/// @param buffer The buffer to receive the bytes written. It is assumed that
/// the buffer is large enough to hold the 4 bytes that are written.
void WriteUInt32BigEndian(Byte* buffer, UInt32 value) {
  for (int i = 3; i >= 0; i--) {
    *buffer++ = ((value >> (8 * i)) & 0xff);
  }
}

/// Checks to make sure the bytes buffer, num_bytes and start index values are
/// valid individually and with respect to each other. If not, returns one of
/// IsoAtom decode error values. If they are valid, then it creates a data
/// segment with the bytes and a data range of [0, num_bytes].
/// @param bytes The byte buffer to decode
/// @param num_bytes The number of bytes in the buffer.
/// @param start The index at which to start decoding.
/// @param segment The data segment created from the bytes.
/// @return kDecodeOk if all is well, otherwise the error code.
int CheckBytes(const Byte* bytes, UInt64 num_bytes, UInt64 start,
               std::shared_ptr<DataSegment>& segment) {
  if (bytes == nullptr) {
    return IsoAtom::kDecodeErrorInvalidDataSource;
  }
  if (start >= num_bytes) {
    return IsoAtom::kDecodeEndOfData;
  }
  UInt64 bytes_remaining = num_bytes - start;
  if (bytes_remaining < IsoAtom::kStandardHeaderLength) {
    return IsoAtom::kDecodeErrorPrematureEndOfData;
  }
  segment = DataSegment::Create(DataRange(0, num_bytes), bytes,
                                DataSegment::kDontDelete);
  if (segment == nullptr) {
    return IsoAtom::kDecodeErrorInvalidDataSource;
  }
  return IsoAtom::kDecodeOk;
}

}  // namespace

UInt32 IsoAtom::GetMinHeaderLength(UInt64 payload_length) {
  return payload_length > kMaxPayloadForStandardHeader ? kExtendedHeaderLength
                                                       : kStandardHeaderLength;
}

int IsoAtom::Scan(const std::vector<Byte>& bytes, UInt64 start,
                  ScanReceiver scan_receiver) {
  if (start >= bytes.size()) {
    return kDecodeEndOfData;
  }
  return Scan(bytes.data(), bytes.size(), start, std::move(scan_receiver));
}

int IsoAtom::Scan(const Byte* bytes, UInt64 num_bytes, UInt64 start,
                  ScanReceiver scan_receiver) {
  std::shared_ptr<DataSegment> segment;
  int status = CheckBytes(bytes, num_bytes, start, segment);
  if (status != kDecodeOk) {
    return status;
  }
  DataSegmentDataSource source(segment);
  return Scan(&source, start, std::move(scan_receiver));
}

int IsoAtom::Scan(DataSource* data_source, UInt64 start,
                  ScanReceiver scan_receiver) {
  if (data_source == nullptr) {
    return kDecodeErrorInvalidDataSource;
  }
  if (scan_receiver == nullptr) {
    return kDecodeErrorInvalidScanReceiver;
  }
  while (true) {
    IsoAtom atom;
    int status = atom.Decode(data_source, start);
    if (status == kDecodeEndOfData) {
      return status;
    } else if (status == kDecodeOk) {
      UInt64 new_start = scan_receiver(start, atom);
      if (new_start == start || atom.IsPayloadSizeToEnd()) {
        return kDecodeOk;
      }
      start = new_start;
    } else {
      return status;
    }
  }
}

bool IsoAtom::IsValid() const {
  if (type_.length() != kTypeLength) {
    return false;
  } else if (header_length_ != kStandardHeaderLength &&
             header_length_ != kExtendedHeaderLength) {
    return false;
  } else if (header_length_ == kStandardHeaderLength &&
             payload_length_ > kMaxPayloadForStandardHeader) {
    return false;
  } else if (header_length_ == kExtendedHeaderLength &&
             payload_length_ > kMaxPayloadForExtendedHeader) {
    return false;
  }
  return true;
}

void IsoAtom::ReduceHeaderLengthIfPossible() {
  header_length_ = GetMinHeaderLength(payload_length_);
}

std::vector<Byte> IsoAtom::Encode() const { return EncodeWorker(); }

std::vector<Byte> IsoAtom::EncodeWorker() const {
  std::vector<Byte> bytes;
  if (!IsValid()) {
    return bytes;
  }
  Byte four_bytes[4];
  const Byte* type_as_bytes = reinterpret_cast<const Byte*>(type_.c_str());
  if (header_length_ == kStandardHeaderLength) {
    UInt32 atom_length =
        payload_length_ > 0 ? static_cast<UInt32>(GetLength()) : 0;
    WriteUInt32BigEndian(four_bytes, atom_length);
    bytes.insert(bytes.end(), four_bytes, four_bytes + sizeof(four_bytes));
    bytes.insert(bytes.end(), type_as_bytes, type_as_bytes + kTypeLength);
  } else {
    const UInt32 kLargeSignal = 1;
    Byte eight_bytes[8];
    WriteUInt32BigEndian(four_bytes, kLargeSignal);
    bytes.insert(bytes.end(), four_bytes, four_bytes + sizeof(four_bytes));
    bytes.insert(bytes.end(), type_as_bytes, type_as_bytes + kTypeLength);
    UInt64 atom_length = payload_length_ + kExtendedHeaderLength;
    WriteUInt64BigEndian(eight_bytes, atom_length);
    bytes.insert(bytes.end(), eight_bytes, eight_bytes + sizeof(eight_bytes));
  }
  return bytes;
}

int IsoAtom::Decode(const std::vector<Byte>& bytes, UInt64 start) {
  if (start >= bytes.size()) {
    return kDecodeEndOfData;
  }
  return Decode(bytes.data(), bytes.size(), start);
}

int IsoAtom::Decode(const Byte* bytes, UInt64 num_bytes, UInt64 start) {
  clear();
  std::shared_ptr<DataSegment> segment;
  int status = CheckBytes(bytes, num_bytes, start, segment);
  if (status != kDecodeOk) {
    return status;
  }
  DataSegmentDataSource source(segment);
  return Decode(&source, start);
}

int IsoAtom::Decode(DataSource* data_source, UInt64 start) {
  if (data_source == nullptr) {
    return kDecodeErrorInvalidDataSource;
  }
  return DecodeWorker(data_source, start);
}

void IsoAtom::clear() {
  type_.clear();
  header_length_ = 0;
  payload_length_ = 0;
}

int IsoAtom::DecodeWorker(DataSource* data_source, UInt64 start) {
  clear();
  std::vector<Byte> bytes(kExtendedHeaderLength);
  DataRange range(start, start + bytes.size());
  BytePointerDataDestination destination(bytes.data(), bytes.size());
  destination.StartTransfer();
  data_source->TransferData(range, range.GetLength(), &destination);
  size_t num_bytes_transferred = destination.GetBytesTransferred();
  if (num_bytes_transferred == 0) {
    return kDecodeEndOfData;
  } else if (num_bytes_transferred < IsoAtom::kStandardHeaderLength) {
    return kDecodeErrorPrematureEndOfData;
  }

  UInt32 header_length = kStandardHeaderLength;
  UInt64 atom_length = ReadUInt32BigEndian(bytes.data());
  const char* data_as_char = reinterpret_cast<const char*>(bytes.data());
  std::string atom_type(data_as_char + kTypeOffset, kTypeLength);
  if (atom_length == kExtendedHeaderLengthIndicator) {
    if (num_bytes_transferred < kExtendedHeaderLength) {
      return kDecodeErrorPrematureEndOfData;
    }
    atom_length =
        ReadUInt64BigEndian(bytes.data() + kExtendedHeaderLengthOffset);
    header_length = kExtendedHeaderLength;
  }
  UInt64 payload_length = 0;
  if (atom_length < header_length) {
    if (atom_length == 0 && header_length == kStandardHeaderLength) {
      payload_length = 0;
    } else {
      return kDecodeErrorInvalidAtomLength;
    }
  } else {
    payload_length = atom_length - header_length;
  }
  type_ = atom_type;
  payload_length_ = payload_length;
  header_length_ = header_length;
  return kDecodeOk;
}

}  // namespace image_io
}  // namespace photos_editing_formats
