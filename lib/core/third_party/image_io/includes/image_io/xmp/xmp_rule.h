#ifndef IMAGE_IO_XMP_XMP_RULE_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_RULE_H_  // NOLINT

#include "image_io/xml/xml_reader.h"
#include "image_io/xml/xml_rule.h"

namespace photos_editing_formats {
namespace image_io {

/// An XML rule that implements the XMP specification part 1 syntax.
/// https://wwwimages2.adobe.com/content/dam/acom/en/devnet/xmp/pdfs/XMP%20SDK%20Release%20cc-2016-08/XMPSpecificationPart1.pdf
/// // NOLINT
class XmpRule : public XmlRule {
 public:
  /// @param missing_final_xpacket_flag if not null the final xpacket can be
  ///    missing and this shared bool flag will bet set to true if it is.
  explicit XmpRule(std::shared_ptr<bool> missing_final_xpacket_flag);

  XmpRule();
  bool IsPermissibleToFinish(std::string* error_text) const override;

 private:
  DataMatchResult HandlePostOpenChar(const XmlActionContext& context);
  DataMatchResult HandleComment(const XmlActionContext& context);
  DataMatchResult HandleFirstPi(const XmlActionContext& context);
  DataMatchResult HandleSecondPi(const XmlActionContext& context);
  DataMatchResult HandleOtherPi(const XmlActionContext& context);
  DataMatchResult HandleFirstElement(const XmlActionContext& context);
  DataMatchResult HandleOtherElement(const XmlActionContext& context);
  std::shared_ptr<bool> missing_final_xpacket_flag_;
  size_t pi_rule_count_;
  size_t element_rule_count_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_RULE_H_  // NOLINT
