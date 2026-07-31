#include "image_io/xmp/xmp_rule.h"

#include <string>
#include <utility>

#include "image_io/base/data_scanner.h"
#include "image_io/xml/xml_cdata_and_comment_rules.h"
#include "image_io/xml/xml_element_rules.h"
#include "image_io/xml/xml_handler.h"
#include "image_io/xml/xml_pi_rule.h"
#include "image_io/xml/xml_token_context.h"

namespace photos_editing_formats {
namespace image_io {

namespace {

// Terminal names and parts of terminal names.
const char kWhitespace[] = "Whitespace";

}  // namespace

XmpRule::XmpRule() : XmpRule(std::shared_ptr<bool>()) {}

XmpRule::XmpRule(std::shared_ptr<bool> missing_final_xpacket_flag)
    : XmlRule("Xmp"),
      missing_final_xpacket_flag_(std::move(missing_final_xpacket_flag)),
      pi_rule_count_(0),
      element_rule_count_(0) {
  AddOptionalWhitespaceTerminal();
  AddLiteralTerminal("<");
  AddSentinelTerminal("~?!").WithAction([&](const XmlActionContext& context) {
    return HandlePostOpenChar(context);
  });
  AddOptionalWhitespaceTerminal().WithName(kWhitespace);
}

bool XmpRule::IsPermissibleToFinish(std::string* error_text) const {
  size_t index = GetTerminalIndex();
  if ((index == 0 && pi_rule_count_ == 0 && element_rule_count_ == 1) ||
      index == GetTerminalIndexFromName(kWhitespace)) {
    return true;
  }
  if (element_rule_count_ != 1) {
    *error_text = "While looking for either <rdf:RDF...> or <x:xmpmeta...>";
  } else if (pi_rule_count_ != 2) {
    if (missing_final_xpacket_flag_ != nullptr) {
      *missing_final_xpacket_flag_ = true;
      return true;
    }
    *error_text = "While looking for the final <?xpacket...?>";
  }
  return false;
}

DataMatchResult XmpRule::HandlePostOpenChar(const XmlActionContext& context) {
  char sentinel = context.GetTerminal()->GetScanner()->GetSentinel();
  if (sentinel == '!') {
    return HandleComment(context);
  } else if (sentinel == '?') {
    pi_rule_count_ += 1;
    if ((pi_rule_count_ == 1 && element_rule_count_ == 0)) {
      return HandleFirstPi(context);
    } else if (pi_rule_count_ == 2 && element_rule_count_ == 1) {
      return HandleSecondPi(context);
    } else {
      return HandleOtherPi(context);
    }
  } else if (sentinel == '~') {
    element_rule_count_ += 1;
    if (element_rule_count_ == 1) {
      return HandleFirstElement(context);
    } else {
      return HandleOtherElement(context);
    }
  }
  return context.GetResult();
}

DataMatchResult XmpRule::HandleComment(const XmlActionContext& context) {
  DataMatchResult result = context.GetResult();
  result.SetType(DataMatchResult::kPartial);
  std::unique_ptr<XmlRule> rule(new XmlCommentRule(kSecondStartPoint));
  SetNextRule(std::move(rule));
  ResetTerminalScanners();
  SetTerminalIndex(0);
  return result;
}

DataMatchResult XmpRule::HandleFirstPi(const XmlActionContext& context) {
  DataMatchResult result = context.GetResult();
  result.SetType(DataMatchResult::kPartial);
  std::unique_ptr<XmlRule> rule(new XmlPiRule(kSecondStartPoint));
  SetNextRule(std::move(rule));
  ResetTerminalScanners();
  SetTerminalIndex(0);
  return result;
}

DataMatchResult XmpRule::HandleSecondPi(const XmlActionContext& context) {
  DataMatchResult result = context.GetResult();
  result.SetType(DataMatchResult::kPartial);
  std::unique_ptr<XmlRule> rule(new XmlPiRule(kSecondStartPoint));
  SetNextRule(std::move(rule));
  SetTerminalIndex(GetTerminalIndexFromName(kWhitespace));
  return result;
}

DataMatchResult XmpRule::HandleOtherPi(const XmlActionContext& context) {
  DataMatchResult result = context.GetResult();
  std::string text(
      "Unexpected PI element: one such element may appear before and after "
      "the rdf:RDF element");
  result.SetType(DataMatchResult::kError);
  result.SetMessage(Message::kSyntaxError, context.GetErrorText(text, ""));
  return result;
}

DataMatchResult XmpRule::HandleFirstElement(const XmlActionContext& context) {
  DataMatchResult result = context.GetResult();
  result.SetBytesConsumed(0);
  result.SetType(DataMatchResult::kPartial);
  std::unique_ptr<XmlRule> rule(new XmlElementRule(kSecondStartPoint));
  SetNextRule(std::move(rule));
  ResetTerminalScanners();
  SetTerminalIndex(0);
  return result;
}

DataMatchResult XmpRule::HandleOtherElement(const XmlActionContext& context) {
  DataMatchResult result = context.GetResult();
  std::string text(
      "Unexpected element: only one element is allowed in the XMP packet");
  result.SetType(DataMatchResult::kError);
  result.SetMessage(Message::kSyntaxError, context.GetErrorText(text, ""));
  return result;
}

}  // namespace image_io
}  // namespace photos_editing_formats
