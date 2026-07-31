#ifndef IMAGE_IO_XMP_XMP_READER_DATA_DESTINATION_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_READER_DATA_DESTINATION_H_  // NOLINT

#include "image_io/base/data_destination.h"
#include "image_io/xmp/xmp_reader.h"

namespace photos_editing_formats {
namespace image_io {

/// A data destination that sends the bytes it receives into an XmpReader.
class XmpReaderDataDestination : public DataDestination {
 public:
  /// @param reader The reader to parse the bytes this destination receives.
  XmpReaderDataDestination(XmpReader* reader)
      : reader_(reader), bytes_transferred_(0) {}
  void StartTransfer() override;
  TransferStatus Transfer(const DataRange& transfer_range,
                          const DataSegment& data_segment) override;
  void FinishTransfer() override;
  size_t GetBytesTransferred() const override;

 private:
  XmpReader* reader_;
  size_t bytes_transferred_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_READER_DATA_DESTINATION_H_  // NOLINT
