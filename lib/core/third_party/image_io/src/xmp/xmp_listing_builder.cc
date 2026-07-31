#include "image_io/xmp/xmp_listing_builder.h"

#include <algorithm>
#include <iomanip>
#include <string>

#include "image_io/base/data_line_map_builder_destination.h"
#include "image_io/base/ostream_ref_data_destination.h"

namespace photos_editing_formats {
namespace image_io {

using std::ostream;
using std::string;
using std::vector;

bool XmpListingBuilder::Build(DataSource* data_source,
                              const vector<DataRange>& ranges,
                              DataLineMap* data_line_map) {
  const int kNumWidth = 5;
  const string kElide("...");
  const string kColon(": ");
  const size_t kMaxLength =
      std::max(kNumWidth + kElide.length() + kColon.length(), max_line_length_);
  data_line_map->Clear();
  DataLineMapBuilderDestination data_line_map_builder(data_line_map, nullptr);
  data_line_map_builder.StartTransfer();
  for (const auto& range : ranges) {
    data_source->TransferData(range, range.GetLength(), &data_line_map_builder);
  }
  data_line_map_builder.FinishTransfer();

  OStreamRefDataDestination ostream_destination(os_, nullptr);
  ostream_destination.StartTransfer();
  for (const auto& data_line : data_line_map->GetDataLines()) {
    const auto& range = data_line.range;
    os_ << std::setw(kNumWidth) << std::right << data_line.number << kColon;
    if (range.GetLength() <= kMaxLength) {
      data_source->TransferData(range, range.GetLength(), &ostream_destination);
    } else {
      DataRange pre(range.GetBegin(), range.GetBegin() + kMaxLength / 2);
      size_t post_length = kMaxLength / 2 - kElide.length();
      DataRange post(range.GetEnd() - post_length, range.GetEnd());
      data_source->TransferData(pre, pre.GetLength(), &ostream_destination);
      os_ << kElide;
      data_source->TransferData(post, post.GetLength(), &ostream_destination);
    }
    os_ << "\n";
  }

  ostream_destination.FinishTransfer();
  return true;
}

}  // namespace image_io
}  // namespace photos_editing_formats
