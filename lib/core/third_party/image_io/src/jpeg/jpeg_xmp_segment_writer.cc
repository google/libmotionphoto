#include "image_io/jpeg/jpeg_xmp_segment_writer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

#include "image_io/base/string_ref_data_source.h"
#include "image_io/jpeg/jpeg_marker.h"
#include "image_io/jpeg/jpeg_xmp_info.h"

namespace photos_editing_formats {
namespace image_io {

namespace {

// Constants used for segment count/length computations.
const size_t kMaxJpgSegmentSize =
    std::numeric_limits<UInt16>::max() - JpegMarker::kLength;
const size_t kMaxXmpExtensionBytesPerSegment =
    kMaxJpgSegmentSize - kXmpExtendedHeaderSize;
const size_t kXmpExtensionSegmentOverhead =
    kXmpExtendedHeaderSize + JpegMarker::kLength;

// Fragments of the XMP string to write to as a primary XMP segment, with a
// break at the position of the GUID value.
constexpr char kXmpPrimary1[] = R"(
<x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="Adobe XMP Core 5.1.0-jc003">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about="" xmlns:xmpNote="http://ns.adobe.com/xmp/note/">
      <xmpNote:HasExtendedXMP>)";
constexpr char kXmpPrimary2[] = R"(</xmpNote:HasExtendedXMP>
    </rdf:Description>
  </rdf:RDF>
</x:xmpmeta>
)";

// Template class for an N byte unsigned BigEndian value with function to
// write it to a stream. Works in conjunction with the << operator below.
template <int N>
struct BigEndian {
  size_t value;
  explicit BigEndian(size_t v) : value(v) {}
  std::ostream& write(std::ostream& os) const {
    const int kMaxN = 4;
    const int kShift[kMaxN] = {0, 8, 16, 24};
    for (int i = std::min(N, kMaxN) - 1; i >= 0; --i) {
      os << static_cast<char>((value >> kShift[i]) & 0xFF);
    }
    return os;
  }
};

// Template insertion operator for use with BigEndian<N> template class.
template <int N>
std::ostream& operator<<(std::ostream& os, const BigEndian<N>& value) {
  return value.write(os);
}

}  // namespace

size_t JpegXmpSegmentWriter::GetExtendedSegmentCount(size_t extended_size) {
  return std::ceil(static_cast<double>(extended_size) /
                   kMaxXmpExtensionBytesPerSegment);
}

bool JpegXmpSegmentWriter::WriteStandardPrimarySegment(
    const std::string& guid, DataDestination* destination) {
  if (!CheckGuid(guid)) {
    return false;
  }
  size_t data_size = 1 + strlen(kXmpId) + strlen(kXmpPrimary1) +
                     strlen(kXmpPrimary2) + kXmpGuidSize + JpegMarker::kLength;
  std::stringstream data_stream;
  data_stream << BigEndian<1>(JpegMarker::kStart);
  data_stream << BigEndian<1>(JpegMarker::kAPP1);
  data_stream << BigEndian<2>(data_size);
  data_stream << kXmpId;
  data_stream << BigEndian<1>(0);
  data_stream << kXmpPrimary1;
  data_stream << guid;
  data_stream << kXmpPrimary2;
  auto data_string = data_stream.str();
  StringRefDataSource data_source(data_string);
  DataRange data_range(0, data_string.length());
  if (!Transfer(&data_source, data_range, destination)) {
    return false;
  }
  return true;
}

bool JpegXmpSegmentWriter::WriteExtendedSegments(const std::string& guid,
                                                 DataSource* source,
                                                 const DataRange& range,
                                                 DataDestination* destination,
                                                 size_t* segments_written) {
  *segments_written = 0;
  if (!CheckGuid(guid)) {
    return false;
  }
  if (!range.IsValid()) {
    ReportInternalError(
        "JpegXmpSegmentWriter::WriteExtendedXmpSegments called with an invalid "
        "or zero-length data range");
    return false;
  }
  size_t range_end = range.GetEnd();
  size_t range_length = range.GetLength();
  size_t count = GetExtendedSegmentCount(range_length);
  for (size_t index = 0; index < count; ++index) {
    size_t begin = range.GetBegin() + index * kMaxXmpExtensionBytesPerSegment;
    size_t end = std::min(range_end, begin + kMaxXmpExtensionBytesPerSegment);
    size_t segment_size = end - begin + kXmpExtensionSegmentOverhead;
    std::stringstream header_stream;
    header_stream << BigEndian<1>(JpegMarker::kStart);
    header_stream << BigEndian<1>(JpegMarker::kAPP1);
    header_stream << BigEndian<2>(segment_size);
    header_stream << kXmpExtendedId;
    header_stream << BigEndian<1>(0);
    header_stream << guid;
    header_stream << BigEndian<4>(range_length);
    header_stream << BigEndian<4>(begin);
    auto header_string = header_stream.str();
    StringRefDataSource header_source(header_string);
    DataRange header_range(0, header_string.length());
    if (!Transfer(&header_source, header_range, destination)) {
      return false;
    }
    if (!Transfer(source, DataRange(begin, end), destination)) {
      return false;
    }
  }
  *segments_written = count;
  return true;
}

bool JpegXmpSegmentWriter::CheckGuid(const std::string& guid) {
  if (guid.length() == kXmpGuidSize) {
    return true;
  }
  std::stringstream ss;
  ss << "JpegXmpSegmentWriter: invalid GUID value or length: " << guid;
  ReportInternalError(ss.str());
  return false;
}

void JpegXmpSegmentWriter::ReportInternalError(const std::string& text) {
  if (message_handler_ != nullptr) {
    message_handler_->ReportMessage(Message::kInternalError, text);
  }
}

bool JpegXmpSegmentWriter::Transfer(DataSource* source, const DataRange& range,
                                    DataDestination* destination) {
  size_t old_byte_count = destination->GetBytesTransferred();
  auto result = source->TransferData(range, range.GetLength(), destination);
  if (result == DataSource::kTransferDataSuccess) {
    size_t bytes_transferred =
        destination->GetBytesTransferred() - old_byte_count;
    if (bytes_transferred != range.GetLength()) {
      result = DataSource::kTransferDataError;
      if (message_handler_) {
        std::stringstream ss;
        ss << "JpegXmpSegmentWriter:data source transferred "
           << bytes_transferred << " bytes instead of " << range.GetLength();
        message_handler_->ReportMessage(Message::kInternalError, ss.str());
      }
    }
  }
  return result == DataSource::kTransferDataSuccess;
}

}  // namespace image_io
}  // namespace photos_editing_formats
