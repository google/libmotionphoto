#include "image_io/xmp/xmp_gain_map_metadata_reader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "image_io/base/data_match_result.h"
#include "image_io/base/validated_number.h"
#include "image_io/xml/xml_token_context.h"
#include "image_io/xmp/xmp_content_handler.h"
#include "image_io/xmp/xmp_element_handler.h"
#include "image_io/xmp/xmp_errors.h"
#include "image_io/xmp/xmp_gain_map_metadata.h"
#include "image_io/xmp/xmp_helpers.h"
#include "image_io/xmp/xmp_rdf_constants.h"
#include "image_io/xmp/xmp_value.h"

namespace photos_editing_formats {
namespace image_io {

namespace {

using Float3 = std::array<float, 3>;

//   a: The first string to compare.
//   b: The second string constant to compare.
// Returns whether the strings are equal ignoring case.
bool EqualNoCase(std::string a, const char* b) {
  return std::equal(a.begin(), a.end(), b,
                    [](char a, char b) { return tolower(a) == tolower(b); });
}

//   str: The string with commas to be split.
// Returns a vector with the substrings taken from str.
std::vector<std::string> SplitCommaString(const std::string& str) {
  std::vector<std::string> result;
  std::stringstream strstr(str);
  for (std::string segment; std::getline(strstr, segment, ','); /*empty*/) {
    result.push_back(segment);
  }
  return result;
}

// A template function that can be used to assign a string value to a boolean
// XmpValue, and return a result with an error message if the string could not
// be converted to the bool type.
//   str_value: The string value to use for the assignment.
//   value_name: The name of the value used for the error message.
//   context: The context used to generate the error message.
//   value: The XmpValue to receive the converted string value.
// Returns a result that has an error message if the string value could not
// be coverted, or if the XmpValue already had a value.
inline DataMatchResult SetXmpValue(const std::string& str_value,
                                   const std::string& value_name,
                                   const XmlTokenContext& context,
                                   XmpValue<bool>* value) {
  if (value->WasAssigned()) {
    return GetNameAlreadyAssignedErrorResult(value_name, context);
  }
  if (str_value == "1" || EqualNoCase(str_value, "true")) {
    *value = true;
  } else if (str_value == "0" || EqualNoCase(str_value, "false")) {
    *value = false;
  } else {
    return GetInvalidValueErrorResult(value_name, {"1", "true", "0", "false"},
                                      context);
  }
  return context.GetResult();
}

// A template function that can be used to assign a string value to an
// array<float, 3> type XmpValue, and return a result with an error message if
// the string could not be converted to the bool type.
//   str_value: The string value to use for the assignment.
//   value_name: The name of the value used for the error message.
//   value_type: The expected type of value.
//   context: The context used to generate the error message.
//   value: The XmpValue to receive the converted string value.
// Returns a result that has an error message if the string value could not
// be coverted, or if the XmpValue already had a value.
inline DataMatchResult SetXmpValue(const std::string& str_value,
                                   const std::string& value_name,
                                   ValidatedValueType value_type,
                                   const XmlTokenContext& context,
                                   XmpValue<Float3>* value) {
  if (value->WasAssigned()) {
    return GetNameAlreadyAssignedErrorResult(value_name, context);
  }
  auto strValues = SplitCommaString(str_value);
  size_t valueCount = strValues.size();
  if (valueCount == 1 || valueCount == 3) {
    Float3 arrayValue;
    for (size_t index = 0; index < valueCount; ++index) {
      XmpValue<float> tempValue;
      auto result = SetXmpValue(strValues[index], value_name, value_type,
                                context, &tempValue);
      if (!tempValue.IsValid()) return result;
      arrayValue[index] = tempValue.GetValue();
    }
    if (valueCount == 1) {
      arrayValue[1] = arrayValue[0];
      arrayValue[2] = arrayValue[0];
    }
    value->SetValue(arrayValue);
  } else {
    return GetInvalidValueErrorResult(value_name,
                                      {"Float", "Float,Float,Float"}, context);
  }
  return context.GetResult();
}

// A shorter version of JoinPrefixAndName.
std::string Name(const std::string& prefix, const std::string& name) {
  return JoinPrefixAndName(prefix, name);
}

}  // namespace

XmpGainMapMetadataReader::XmpGainMapMetadataReader() {
  AddUri(kXmpGainMapUri);
  AddUri(kXmpRdfUri);
}

void XmpGainMapMetadataReader::SetUriPrefix(const std::string& uri,
                                            const std::string& prefix) {
  float_value_map_.clear();
  float3_value_map_.clear();
  supported_name_value_type_map_.clear();
  XmpContent::Type kValue = XmpContent::kValue;
  if (uri == kXmpGainMapUri) {
    SetPrefix(kXmpGainMapPrefix, prefix);
    AddSupportedName(Name(prefix, kXmpGainMapVersion), kValue);
    AddSupportedName(Name(prefix, kXmpGainMapBaseRenditionIsHDR), kValue);
    AddSupportedFloatName(Name(prefix, kXmpGainMapHDRCapacityMin),
                          &gain_map_metadata_.hdr_capacity_min, kValidValueGe0);
    AddSupportedFloatName(Name(prefix, kXmpGainMapHDRCapacityMax),
                          &gain_map_metadata_.hdr_capacity_max, kValidValueGe0);
    AddSupportedFloat3Name(Name(prefix, kXmpGainMapGainMapMin),
                           &gain_map_metadata_.gain_map_min, kValidValue);
    AddSupportedFloat3Name(Name(prefix, kXmpGainMapGainMapMax),
                           &gain_map_metadata_.gain_map_max, kValidValueGe0);
    AddSupportedFloat3Name(Name(prefix, kXmpGainMapGamma),
                           &gain_map_metadata_.gamma, kValidValueGt0);
    AddSupportedFloat3Name(Name(prefix, kXmpGainMapOffsetSDR),
                           &gain_map_metadata_.offset_sdr, kValidValueGe0);
    AddSupportedFloat3Name(Name(prefix, kXmpGainMapOffsetHDR),
                           &gain_map_metadata_.offset_hdr, kValidValueGe0);
  } else if (uri == kXmpRdfUri) {
    SetPrefix(kXmpRdfPrefix, prefix);
    AddSupportedName(Name(prefix, kXmpRdfSeq), kValue);
    AddSupportedName(Name(prefix, kXmpRdfLi), kValue);
  }
}

void XmpGainMapMetadataReader::AddSupportedFloatName(const std::string& name,
                                                     XmpFloatValue* value,
                                                     ValidatedValueType type) {
  AddSupportedNameAndValueType(name, type);
  float_value_map_.insert({name, value});
}

void XmpGainMapMetadataReader::AddSupportedFloat3Name(const std::string& name,
                                                      XmpFloat3Value* value,
                                                      ValidatedValueType type) {
  AddSupportedNameAndValueType(name, type);
  float3_value_map_.insert({name, value});
}

void XmpGainMapMetadataReader::AddSupportedNameAndValueType(
    const std::string& name, ValidatedValueType type) {
  AddSupportedName(name, XmpContent::kValue);
  supported_name_value_type_map_.insert({name, type});
}

std::string XmpGainMapMetadataReader::GetListElementName(
    const StringVector& element_name_stack, size_t index_from_back) const {
  size_t stack_size = element_name_stack.size();
  return stack_size < index_from_back
             ? "?"
             : element_name_stack[stack_size - index_from_back];
}

ValidatedValueType XmpGainMapMetadataReader::GetValueType(
    const std::string& name) const {
  auto pos = supported_name_value_type_map_.find(name);
  if (pos == supported_name_value_type_map_.end()) {
    return kInvalidValue;
  }
  return pos->second;
}

// This function gets called at the start of a Seq/Li.
DataMatchResult XmpGainMapMetadataReader::ProcessRdfSeqName(
    const StringVector& element_name_stack, const std::string& element_name,
    const XmlTokenContext& context) {
  const auto& new_list_element_name = GetListElementName(element_name_stack, 1);
  auto pos = float3_value_map_.find(new_list_element_name);
  if (pos == float3_value_map_.end() || !list_element_name_.empty()) {
    return GetInvalidListErrorResult(new_list_element_name, context);
  }
  list_element_name_ = new_list_element_name;
  list_float_values_.clear();
  return context.GetResult();
}

// This function gets called with the value of a Li element.
DataMatchResult XmpGainMapMetadataReader::ProcessRdfSeqLiContent(
    const StringVector& element_name_stack, const std::string& element_name,
    const XmpContent& content, const XmlTokenContext& context) {
  const auto& this_element_name = GetListElementName(element_name_stack, 2);
  if (this_element_name != list_element_name_ ||
      list_float_values_.size() >= 3) {
    return GetTooManyListLementsErrorResult(this_element_name, 3, context);
  }
  auto pos = float3_value_map_.find(this_element_name);
  if (pos == float3_value_map_.end()) {
    return GetInvalidListErrorResult(this_element_name, context);
  }
  auto& value = list_float_values_.emplace_back();
  auto result = SetXmpValue(content.value, this_element_name, context, &value);
  if (!value.IsValid()) return result;
  auto value_type = GetValueType(this_element_name);
  value.SetValid(ValidateValueType(value.GetValidatedNumber(), value_type));
  if (!value.IsValid()) {
    return GetInvalidNumericalValueErrorResult(this_element_name, context,
                                               value_type);
  }
  return context.GetResult();
}

// This function gets called at the end of a Seq/Li.
DataMatchResult XmpGainMapMetadataReader::ProcessRdfSeqContent(
    const StringVector& element_name_stack, const std::string& element_name,
    const XmpContent& content, const XmlTokenContext& context) {
  const auto& this_element_name = GetListElementName(element_name_stack, 1);
  if (this_element_name != list_element_name_) {
    return GetInvalidListErrorResult(this_element_name, context);
  }
  auto pos = float3_value_map_.find(this_element_name);
  if (pos == float3_value_map_.end()) {
    return GetInvalidListErrorResult(this_element_name, context);
  }
  auto float3Value = pos->second;
  if (float3Value->WasAssigned()) {
    return GetNameAlreadyAssignedErrorResult(this_element_name, context);
  }
  size_t float_list_size = list_float_values_.size();
  if (float_list_size == 1) {
    float value = list_float_values_[0].GetValue();
    float3Value->SetValue({value, value, value});
    float3Value->SetValid(list_float_values_[0].IsValid());
  } else if (float_list_size == 3) {
    float3Value->SetValue({list_float_values_[0].GetValue(),
                           list_float_values_[1].GetValue(),
                           list_float_values_[2].GetValue()});
    float3Value->SetValid(list_float_values_[0].IsValid() &&
                          list_float_values_[1].IsValid() &&
                          list_float_values_[2].IsValid());
  } else {
    // An error was already issued for this case in the ProcessRdfSeqLiContent
    // function, so just set the xmp value's assigned and valid values here.
    float3Value->SetAssigned(true);
    float3Value->SetValid(false);
  }
  return context.GetResult();
}

DataMatchResult XmpGainMapMetadataReader::ProcessElementName(
    const StringVector& element_name_stack, const std::string& element_name,
    const XmlTokenContext& context) {
  if (element_name == list_element_name_) {
    list_element_name_.clear();
    return context.GetResult();
  }
  if (IsPrefixedName(element_name, kXmpRdfPrefix, kXmpRdfSeq)) {
    return ProcessRdfSeqName(element_name_stack, element_name, context);
  }
  return context.GetResult();
}

DataMatchResult XmpGainMapMetadataReader::ProcessElementContent(
    const StringVector& element_name_stack, const std::string& element_name,
    const XmpContent& content, const XmlTokenContext& context) {
  std::string value = content.value;
  // If the element being set is the list element that was just processed, then
  // there is nothing more to do other than clear the list element name.
  if (element_name == list_element_name_) {
    list_element_name_.clear();
    return context.GetResult();
  }

  // Set the float3 value if that is what the element name represents.
  auto float3_pos = float3_value_map_.find(element_name);
  if (float3_pos != float3_value_map_.end()) {
    return SetXmpValue(content.value, element_name, GetValueType(element_name),
                       context, float3_pos->second);
  }

  // Set the float value if that is what the element name represents.
  auto float_pos = float_value_map_.find(element_name);
  if (float_pos != float_value_map_.end()) {
    return SetXmpValue(content.value, element_name, GetValueType(element_name),
                       context, float_pos->second);
  }

  // Otherwise check if its Rdf related or the version or base rendition.
  if (IsPrefixedName(element_name, kXmpRdfPrefix, kXmpRdfLi)) {
    return ProcessRdfSeqLiContent(element_name_stack, element_name, content,
                                  context);
  } else if (IsPrefixedName(element_name, kXmpRdfPrefix, kXmpRdfSeq)) {
    return ProcessRdfSeqContent(element_name_stack, element_name, content,
                                context);
  } else if (IsPrefixedName(element_name, kXmpGainMapPrefix,
                            kXmpGainMapVersion)) {
    return SetXmpValue(content.value, element_name, context,
                       &gain_map_metadata_.version);
  } else if (IsPrefixedName(element_name, kXmpGainMapPrefix,
                            kXmpGainMapBaseRenditionIsHDR)) {
    return SetXmpValue(content.value, element_name, context,
                       &gain_map_metadata_.base_rendition_is_hdr);
  }
  return context.GetResult();
}

}  // namespace image_io
}  // namespace photos_editing_formats
