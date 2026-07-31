#ifndef IMAGE_IO_JPEG_JPEG_XMP_SEGMENT_WRITER_H_  // NOLINT
#define IMAGE_IO_JPEG_JPEG_XMP_SEGMENT_WRITER_H_  // NOLINT

#include <string>

#include "image_io/base/data_destination.h"
#include "image_io/base/data_range.h"
#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"

namespace photos_editing_formats {
namespace image_io {

/// Writes XMP data to JPEG segments. Primary use case is currrently for writing
/// a standard primary segment with a GUID that refers to XMP data in one or
/// more extended segments.
class JpegXmpSegmentWriter {
 public:
  // @param extended_size The length of the extended XMP data
  // @return The number of extended XMP Jpeg segments to write the data.
  static size_t GetExtendedSegmentCount(size_t extended_size);

  /// @param message_handler The message handler for error messages.
  explicit JpegXmpSegmentWriter(MessageHandler* message_handler)
      : message_handler_(message_handler) {}

  /// @param guid The guid value for the extended XMP data.
  /// @param destination The destination to write the JPEG segment to.
  /// @return Whether the segment was written successfully.
  bool WriteStandardPrimarySegment(const std::string& guid,
                                   DataDestination* destination);

  /// @param guid The guid value for the extended XMP data.
  /// @param source The data source for the extended XMP data.
  /// @param range The range in the data source where the XMP data is.
  /// @param destination The destination to write the JPEG segment to.
  /// @return Whether the segment was written successfully.
  bool WriteExtendedSegments(const std::string& guid, DataSource* source,
                             const DataRange& range,
                             DataDestination* destination,
                             size_t* segments_written);

 private:
  bool CheckGuid(const std::string& guid);
  void ReportInternalError(const std::string& text);
  bool Transfer(DataSource* source, const DataRange& range,
                DataDestination* destination);
  MessageHandler* message_handler_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_JPEG_JPEG_XMP_SEGMENT_WRITER_H_  // NOLINT
