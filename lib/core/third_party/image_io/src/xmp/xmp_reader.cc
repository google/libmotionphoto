#include "image_io/xmp/xmp_reader.h"

#include "image_io/xmp/xmp_rule.h"

namespace photos_editing_formats {
namespace image_io {

XmpReader::XmpReader(MessageHandler* message_handler)
    : reader_(&handler_, message_handler) {}

void XmpReader::SetIgnoreMissingFinalXpacket() {
  missing_final_xpacket_flag_.reset(new bool);
  *missing_final_xpacket_flag_ = false;
}

bool XmpReader::IsMissingFinalXpacket() const {
  return missing_final_xpacket_flag_ != nullptr && *missing_final_xpacket_flag_;
}

bool XmpReader::StartParse() {
  return reader_.StartParse(
      std::unique_ptr<XmlRule>(new XmpRule(missing_final_xpacket_flag_)));
}

bool XmpReader::FinishParse() {
  bool reader_finish = reader_.FinishParse();
  bool handler_finish = handler_.FinishParse();
  return reader_finish && handler_finish;
}

bool XmpReader::HasErrors() const { return reader_.HasErrors(); }

}  // namespace image_io
}  // namespace photos_editing_formats
