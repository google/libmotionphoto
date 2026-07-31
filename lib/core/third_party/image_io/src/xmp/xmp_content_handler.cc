#include "image_io/xmp/xmp_content_handler.h"

#include "image_io/xmp/xmp_errors.h"
#include "image_io/xmp/xmp_helpers.h"

namespace photos_editing_formats {
namespace image_io {

using std::string;
using std::vector;

XmpContentHandler::XmpContentHandler() : building_attribute_value_(false) {}

void XmpContentHandler::AddSupportedName(const string& name,
                                         XmpContent::Type type) {
  supported_name_types_[name] = type;
}

XmpContent::Type XmpContentHandler::GetSupportedNameType(
    const string& name) const {
  auto pos = supported_name_types_.find(name);
  return pos == supported_name_types_.end() ? XmpContent::kNone : pos->second;
}

bool XmpContentHandler::HasPrefix(const string& prefixed_name,
                                  const string& prefix_key) const {
  auto pos = prefixes_.find(prefix_key);
  const auto& key = pos == prefixes_.end() ? prefix_key : pos->second;
  return prefixed_name.find(key) == 0 &&      // NOLINT
         prefixed_name[key.length()] == ':';  // NOLINT
}

bool XmpContentHandler::IsPrefixedName(const string& prefixed_name,
                                       const string& prefix_key,
                                       const string& name) const {
  size_t name_len = name.length();
  size_t prefixed_name_len = prefixed_name.length();
  if (name_len >= prefixed_name_len || 1 >= prefixed_name_len - name_len ||
      prefixed_name[prefixed_name_len - name_len - 1] != ':') {
    return false;
  }
  for (size_t index = 1; index <= name_len; ++index) {
    if (name[name_len - index] != prefixed_name[prefixed_name_len - index]) {
      return false;
    }
  }
  return HasPrefix(prefixed_name, prefix_key);
}

string XmpContentHandler::GetPrefixedName(const string& prefix_key,
                                          const string& name) const {
  auto pos = prefixes_.find(prefix_key);
  return JoinPrefixAndName(pos == prefixes_.end() ? prefix_key : pos->second,
                           name);
}

DataMatchResult XmpContentHandler::StartElement(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  element_contents_.push_back(XmpContent());
  if (deprecated_names_.count(element_name)) {
    return GetDeprecatedNameErrorResult(element_name, context);
  } else {
    auto pos = supported_name_types_.find(element_name);
    if (pos != supported_name_types_.end()) {
      element_contents_.back().type = pos->second;
    }
  }
  return ProcessElementName(element_name_stack, element_name, context);
}

DataMatchResult XmpContentHandler::FinishElement(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  auto result = context.GetResult();
  if (element_contents_.back().type != XmpContent::kNone) {
    result = ProcessElementContent(element_name_stack, element_name,
                                   element_contents_.back(), context);
  }
  element_contents_.pop_back();
  return result;
}

DataMatchResult XmpContentHandler::AttributeName(
    const StringVector& element_name_stack, const string& element_name,
    const string& attribute_name, const XmlTokenContext& context) {
  attribute_value_range_ = XmpContent();
  if (deprecated_names_.count(attribute_name)) {
    return GetDeprecatedNameErrorResult(attribute_name, context);
  }
  auto pos = supported_name_types_.find(attribute_name);
  if (pos != supported_name_types_.end()) {
    attribute_value_range_.type = pos->second;
  }
  return ProcessAttributeName(element_name_stack, element_name, attribute_name,
                              context);
}

DataMatchResult XmpContentHandler::AttributeValue(
    const StringVector& element_name_stack, const string& element_name,
    const string& attribute_name, const XmlTokenContext& context) {
  const bool kRemoveQuotes = true;
  auto& value_range = attribute_value_range_;
  bool done = false;
  if (value_range.HasValue() &&
      context.BuildTokenValue(&value_range.value, kRemoveQuotes)) {
    done = true;
  }
  if (value_range.HasRange() &&
      context.BuildTokenValueRanges(&value_range.ranges, kRemoveQuotes)) {
    done = true;
  }
  if (done) {
    auto result =
        ProcessAttributeValue(element_name_stack, element_name, attribute_name,
                              attribute_value_range_, context);
    attribute_value_range_ = XmpContent();
    return result;
  }
  return context.GetResult();
}

DataMatchResult XmpContentHandler::ElementContent(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  auto& content_value_range = element_contents_.back();
  if (content_value_range.HasValue()) {
    string content;
    context.BuildTokenValue(&content);
    content_value_range.value += content;
  }
  if (content_value_range.HasRange()) {
    vector<DataRange> content_range;
    context.BuildTokenValueRanges(&content_range);
    if (!content_range.empty()) {
      content_value_range.ranges.insert(content_value_range.ranges.end(),
                                        content_range.begin(),
                                        content_range.end());
    }
  }
  return context.GetResult();
}

DataMatchResult XmpContentHandler::ProcessElementName(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  return context.GetResult();
}

DataMatchResult XmpContentHandler::ProcessElementContent(
    const StringVector& element_name_stack, const string& element_name,
    const XmpContent& element_content, const XmlTokenContext& context) {
  return context.GetResult();
}

DataMatchResult XmpContentHandler::ProcessAttributeName(
    const StringVector& element_name_stack, const string& element_name,
    const string& attribute_name, const XmlTokenContext& context) {
  return context.GetResult();
}

DataMatchResult XmpContentHandler::ProcessAttributeValue(
    const StringVector& element_name_stack, const string& element_name,
    const string& attribute_name, const XmpContent& attribute_content,
    const XmlTokenContext& context) {
  StringVector stack(element_name_stack);
  stack.push_back(element_name);
  return ProcessElementContent(stack, attribute_name, attribute_content,
                               context);
}

}  // namespace image_io
}  // namespace photos_editing_formats
