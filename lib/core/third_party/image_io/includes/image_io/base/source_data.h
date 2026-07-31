#ifndef IMAGE_IO_BASE_SOURCE_DATA_H_  // NOLINT
#define IMAGE_IO_BASE_SOURCE_DATA_H_  // NOLINT

#include <memory>

#include "image_io/base/data_destination.h"
#include "image_io/base/data_range.h"
#include "image_io/base/data_source.h"

namespace photos_editing_formats {
namespace image_io {

// A source of data that can be transferred to a destination. Depending on the
// constructor used, it can also manage the life time of the data source.
class SourceData {
 public:
  // The default constructor produces an invalid SourceData object.
  SourceData()
      : shared_data_source_(nullptr),
        data_source_(nullptr),
        data_range_(DataRange()) {}

  //   data_source: The data source.
  //   data_range: A data range in the data source where the source data is
  // located.
  SourceData(DataSource* data_source, DataRange data_range)
      : shared_data_source_(nullptr),
        data_source_(data_source),
        data_range_(data_range) {}

  // This constructor allows the SourceData object so created to manage the
  // lifetime of the data source even when the source data object is copied
  // to another source data object.
  //   data_source: A shared pointer to a data source.
  //   data_range: A data range in the data source where the source data is
  // located.
  SourceData(std::shared_ptr<DataSource> shared_data_source,
             DataRange data_range)
      : shared_data_source_(shared_data_source),
        data_source_(shared_data_source.get()),
        data_range_(data_range) {}

  // Returns whether the source data is valid.
  bool IsValid() const {
    return data_source_ != nullptr && data_range_.IsValid();
  }

  // Returns the data source pointer.
  DataSource* GetDataSource() const { return data_source_; }

  // Returns the data range of the source data.
  DataRange GetDataRange() const { return data_range_; }

  //   destination: The destination to which the source data is to be
  // transferred.
  // Returns the transfer result.
  DataSource::TransferDataResult Transfer(DataDestination* destination) {
    if (destination == nullptr || !IsValid()) {
      return DataSource::TransferDataResult::kTransferDataError;
    }
    return data_source_->TransferData(data_range_, data_range_.GetLength(),
                                      destination);
  }

 private:
  std::shared_ptr<DataSource> shared_data_source_;
  DataSource* data_source_;
  DataRange data_range_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_BASE_SOURCE_DATA_H_  // NOLINT
