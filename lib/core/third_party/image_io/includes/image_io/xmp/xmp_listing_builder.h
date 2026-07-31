#ifndef IMAGE_IO_XMP_XMP_LISTING_BUILDER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_LISTING_BUILDER_H_  // NOLINT

#include <ostream>
#include <vector>

#include "image_io/base/data_line_map.h"
#include "image_io/base/data_range.h"
#include "image_io/base/data_source.h"

namespace photos_editing_formats {
namespace image_io {

/// An XMP string listing builder. The string data is taken from the ranges in
/// the data source and written to the ostream. Each line is prefixed with its
/// line number in the XMP string.
class XmpListingBuilder {
 public:
  /// @param os The output stream to write the listing to.
  /// @param max_line_length The max length of the line written to the ostream.
  /// Lines longer than this value are shortened by removing the middle chars
  /// and replacing them with "...".
  XmpListingBuilder(std::ostream& os, size_t max_line_length)
      : os_(os), max_line_length_(max_line_length) {}

  /// @param data_source The data soruce from which to obtain the XMP strings.
  /// @param ranges The ranges where the XMP strings are located in the source.
  /// @param data_line_map The data line map to receive data line information.
  /// @return Whether the listing was created successfully.
  bool Build(DataSource* data_source, const std::vector<DataRange>& ranges,
             DataLineMap* data_line_map);

 private:
  std::ostream& os_;
  size_t max_line_length_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_LISTING_BUILDER_H_  // NOLINT
