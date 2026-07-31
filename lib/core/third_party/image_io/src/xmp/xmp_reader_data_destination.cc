#include "image_io/xmp/xmp_reader_data_destination.h"

namespace photos_editing_formats {
namespace image_io {

void XmpReaderDataDestination::StartTransfer() { reader_->StartParse(); }

DataDestination::TransferStatus XmpReaderDataDestination::Transfer(
    const DataRange& transfer_range, const DataSegment& data_segment) {
  bytes_transferred_ += transfer_range.GetLength();
  reader_->Parse(transfer_range.GetBegin(), transfer_range, data_segment);
  return kTransferOk;
}

void XmpReaderDataDestination::FinishTransfer() { reader_->FinishParse(); }

size_t XmpReaderDataDestination::GetBytesTransferred() const {
  return bytes_transferred_;
}

}  // namespace image_io
}  // namespace photos_editing_formats
