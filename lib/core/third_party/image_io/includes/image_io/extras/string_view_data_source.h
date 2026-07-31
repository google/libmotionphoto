#ifndef IMAGE_IO_EXTRAS_STRING_VIEW_DATA_SOURCE_H_  // NOLINT
#define IMAGE_IO_EXTRAS_STRING_VIEW_DATA_SOURCE_H_  // NOLINT

#include "image_io/base/data_destination.h"
#include "image_io/base/data_range.h"
#include "image_io/base/data_segment_data_source.h"
#include "third_party/absl/strings/string_view.h"

namespace photos_editing_formats {
namespace image_io {

/// A DataSource that reads bytes from a string_view. The underlying string data
/// must have a lifetime that exceeds the lifetime of this data source, and the
/// string contents must not change while the data source is referencing it.
class StringViewDataSource : public DataSegmentDataSource {
 public:
  /// Constructs a StringViewDataSource using the given string_view.
  /// @param str The string_view to read from.
  explicit StringViewDataSource(absl::string_view string_src);

  /// Returns the string view being used as the data source.
  absl::string_view GetStringView() const { return string_src_; }

 private:
  /// The string_view to read from.
  absl::string_view string_src_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_EXTRAS_STRING_VIEW_DATA_SOURCE_H_  // NOLINT
