#include "image_io/extras/string_view_data_source.h"

#include "image_io/base/data_segment.h"
#include "third_party/absl/strings/string_view.h"

namespace photos_editing_formats {
namespace image_io {

namespace {

/// @param str_view The string_view from which to create a DataSegment.
/// @return A DataSegment the byte pointer of which is taken from the str_view.
std::shared_ptr<DataSegment> CreateDataSegment(absl::string_view str_view) {
  Byte* bytes = reinterpret_cast<Byte*>(const_cast<char*>(str_view.data()));
  return DataSegment::Create(DataRange(0, str_view.length()), bytes,
                             DataSegment::BufferDispositionPolicy::kDontDelete);
}

}  // namespace

StringViewDataSource::StringViewDataSource(absl::string_view string_src)
    : DataSegmentDataSource(CreateDataSegment(string_src)),
      string_src_(string_src) {}

}  // namespace image_io
}  // namespace photos_editing_formats
