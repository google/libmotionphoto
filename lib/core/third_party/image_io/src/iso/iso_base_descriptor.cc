#include "image_io/iso/iso_base_descriptor.h"

#include <sstream>

#include "image_io/base/byte_data.h"

namespace photos_editing_formats {
namespace image_io {

using std::stringstream;

void IsoBaseDescriptor::DecodeTagInstanceSizeAndDetails(IsoDecoder* decoder) {
  if (decoder->DecodeTagValue(GetTag(), GetName())) {
    DecodeInstanceSizeAndDetails(decoder);
  }
}

void IsoBaseDescriptor::DecodeInstanceSizeAndDetails(IsoDecoder* decoder) {
  instance_size_ = decoder->DecodeUIntXValue();
  if (!decoder->HasErrors()) {
    size_t details_next_index = decoder->GetNext();
    size_t details_end_index = details_next_index + instance_size_;
    IsoDecoder details_decoder = decoder->GetSlice(details_end_index);
    DecodeDetails(&details_decoder);
    decoder->CaptureSliceCountsAndMessages(details_decoder);
    if (details_decoder.GetNext() < details_decoder.GetEnd()) {
      if (decoder->IsVerbose()) {
        stringstream ss;
        ss << "The descriptor for " << GetName() << " was not fully decoded - "
           << "decoder instance size " << instance_size_ << " details index "
           << details_next_index << " in range [" << decoder->GetBegin() << ":"
           << decoder->GetEnd() << ")";
        decoder->AddVerboseMessage(ss.str());
      }
      decoder->IncrementIncompleteDecodingCount(1);
    }
    decoder->IncrementNext(instance_size_);
  }
}

}  // namespace image_io
}  // namespace photos_editing_formats
