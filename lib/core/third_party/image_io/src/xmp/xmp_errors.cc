#include "image_io/xmp/xmp_errors.h"

#include <sstream>
#include <string>
#include <vector>

#include "image_io/base/data_match_result.h"
#include "image_io/base/message.h"
#include "image_io/base/validated_number.h"
#include "image_io/xml/xml_token_context.h"

namespace photos_editing_formats {
namespace image_io {

using std::string;
using std::stringstream;
using std::vector;

DataMatchResult GetCdataNotSupportedErrorResult(
    const XmlTokenContext& context) {
  auto result = context.GetResult();
  result.SetMessage(
      Message::kValueError,
      context.GetErrorText("The XML CDATA syntax is not supported", ""));
  return result;
}

DataMatchResult GetDeprecatedNameErrorResult(const string& name,
                                             const XmlTokenContext& context) {
  stringstream ss;
  ss << "Invalid use of a deprecated name or value: " << name;
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

DataMatchResult GetUnrecognizedNameErrorResult(const string& name,
                                               const XmlTokenContext& context) {
  stringstream ss;
  ss << "Unrecognized name: " << name;
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

DataMatchResult GetNameAlreadyAssignedErrorResult(
    const std::string& name, const XmlTokenContext& context) {
  stringstream ss;
  ss << name << " was already assigned a value";
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

DataMatchResult GetInvalidValueErrorResult(
    const std::string& name, const std::vector<std::string>& valid_values,
    const XmlTokenContext& context) {
  stringstream ss;
  ss << "Invalid value for " << name;
  if (!valid_values.empty()) {
    ss << std::endl << "- Valid values are";
    char sep = ':';
    for (const auto& valid_value : valid_values) {
      ss << sep << " '" << valid_value << "'";
      sep = ',';
    }
  }
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

DataMatchResult GetInvalidNumericalValueErrorResult(
    const std::string& name, const XmlTokenContext& context,
    ValidatedValueType expected_number_type) {
  stringstream ss;
  ss << "Expected a ";
  switch (expected_number_type) {
    case kValidValueEq0:
      ss << "zero value";
      break;
    case kValidValueNe0:
      ss << "non-zero value";
      break;
    case kValidValueGt0:
      ss << "value > 0";
      break;
    case kValidValueLt0:
      ss << "value < 0";
      break;
    case kValidValueGe0:
      ss << "value >= 0";
      break;
    case kValidValueLe0:
      ss << "value <= 0";
      break;
    default:
      ss << "numerical value";
      break;
  }
  ss << " for " << name;
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

DataMatchResult GetNamePathErrorResult(const string& name,
                                       const vector<string>& element_name_stack,
                                       const string& element_name,
                                       const XmlTokenContext& context) {
  stringstream ss;
  ss << name << " can only be used within an element named: " << element_name;
  for (auto pos = element_name_stack.crbegin();
       pos != element_name_stack.crend(); ++pos) {
    ss << std::endl << "- contained in an element named: " << *pos;
  }
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

DataMatchResult GetDuplicateElementErrorResult(const std::string& name,
                                               const XmlTokenContext& context) {
  stringstream ss;
  ss << "Duplicate element: " << name;
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

DataMatchResult GetCantBeAnAttributeErrorResult(
    const std::string& name, const XmlTokenContext& context) {
  stringstream ss;
  ss << "This name can't be used as an attribute: " << name;
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

DataMatchResult GetInvalidListErrorResult(const std::string& name,
                                          const XmlTokenContext& context) {
  stringstream ss;
  ss << "The list value for this element is invalid: " << name;
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

DataMatchResult GetTooManyListLementsErrorResult(
    const std::string& name, size_t max_size, const XmlTokenContext& context) {
  stringstream ss;
  ss << "The max size for list element " << name << " is " << max_size;
  auto result = context.GetResult();
  result.SetMessage(Message::kValueError, context.GetErrorText(ss.str(), ""));
  return result;
}

}  // namespace image_io
}  // namespace photos_editing_formats
