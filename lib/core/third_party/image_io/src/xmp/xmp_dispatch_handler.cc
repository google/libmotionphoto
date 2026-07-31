#include "image_io/xmp/xmp_dispatch_handler.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace photos_editing_formats {
namespace image_io {

namespace {

using std::string;

const size_t kMaxSize = std::numeric_limits<size_t>::max();
const char kXlmNs[] = "xmlns:";
const char kXmpMetaPrefix[] = "x";
const char kXmpMetaUri[] = "adobe:ns:meta/";
const char kXmpMetaElementName[] = "x:xmpmeta";
const char kRdfPrefix[] = "rdf";
const char kRdfUri[] = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
const char kRdfElementName[] = "rdf:RDF";
const char kRdfDescriptionElementName[] = "rdf:Description";

string GetPrefix(const string& name) {
  size_t colon_pos = name.find(':');
  return colon_pos == string::npos ? "" : name.substr(0, colon_pos);
}

string GetUnrecognizedPrefixErrorText(const string& prefix,
                                      const XmlTokenContext& context) {
  return context.GetErrorText("Unrecognized xmlns prefix:" + prefix, "");
}

string GetPrefixInvalidAtThisLocationErrorText(const string& prefix,
                                               const XmlTokenContext& context) {
  return context.GetErrorText(
      "This xmlns prefix is invalid at this location:" + prefix, "");
}

}  // namespace

string XmpDispatchHandler::GetPrefixRdf() { return kRdfPrefix; }

string XmpDispatchHandler::GetUriForPrefixX() { return kXmpMetaUri; }

string XmpDispatchHandler::GetUriForPrefixRdf() { return kRdfUri; }

XmpDispatchHandler::HandlerData::HandlerData()
    : activation_element_level(kMaxSize),
      activation_element_level_override(false) {}

XmpDispatchHandler::XmpDispatchHandler() : is_ns_element_(false) {
  ns_element_name_set_.insert(kXmpMetaElementName);
  ns_element_name_set_.insert(kRdfElementName);
  ns_element_name_set_.insert(kRdfDescriptionElementName);
  prefix_uri_map_[kXmpMetaPrefix] = kXmpMetaUri;
  prefix_uri_map_[kRdfPrefix] = kRdfUri;
  uri_level_map_[kXmpMetaUri] = 0;
  uri_level_map_[kRdfUri] = 0;
}

void XmpDispatchHandler::AddElementHandler(XmpElementHandler* handler) {
  const auto& uris = handler->GetUris();
  auto& handler_data = handler_data_map_[handler];
  handler_data.uri_set.insert(uris.begin(), uris.end());
  for (auto pos = prefix_uri_map_.begin(); pos != prefix_uri_map_.end();
       ++pos) {
    if (handler_data.uri_set.count(pos->second)) {
      handler->SetUriPrefix(pos->second, pos->first);
    }
  }
  handler->StartParse(this);
}

void XmpDispatchHandler::RemoveElementHandler(XmpElementHandler* handler) {
  handler_data_map_.erase(handler);
}

bool XmpDispatchHandler::FinishParse() {
  bool dispatch_value = true;
  for (auto& handler_entry : handler_data_map_) {
    bool handler_value = handler_entry.first->FinishParse();
    dispatch_value = dispatch_value && handler_value;
  }
  return dispatch_value;
}

DataMatchResult XmpDispatchHandler::StartElement(
    const XmlTokenContext& context) {
  string invalid_xmlns_prefix_error;
  string top_element_name_error_message_text;
  DataMatchResult result = context.GetResult();
  if (context.BuildTokenValue(&build_token_value_)) {
    if (!element_name_.empty()) {
      element_name_stack_.push_back(element_name_);
    }
    if (element_name_stack_.empty() &&
        !(build_token_value_ == kXmpMetaElementName ||
          build_token_value_ == kRdfElementName)) {
      top_element_name_error_message_text = context.GetErrorText(
          "Unexpected top element name:" + build_token_value_,
          string(kXmpMetaElementName) + " or " + kRdfElementName);
    }
    element_name_ = build_token_value_;
    element_uri_ = GetUri(GetPrefix(element_name_));
    is_ns_element_ = ns_element_name_set_.count(element_name_) > 0;
    invalid_xmlns_prefix_error =
        UpdateHandlerActivationLevelsForStartElement(context);
    result = CallElementFunctions(context, &XmpElementHandler::StartElement);
  }
  if (!top_element_name_error_message_text.empty()) {
    result.SetMessage(Message::kValueError,
                      top_element_name_error_message_text);
  }
  if (!invalid_xmlns_prefix_error.empty()) {
    result.SetMessage(Message::kValueError, invalid_xmlns_prefix_error);
  }
  return result;
}

DataMatchResult XmpDispatchHandler::FinishElement(
    const XmlTokenContext& context) {
  string element_name_error_message_text;
  DataMatchResult result = context.GetResult();
  bool done_but_no_name = context.GetTokenPortion() == XmlPortion::kNone;
  if (done_but_no_name || context.BuildTokenValue(&build_token_value_)) {
    if (done_but_no_name) {
      build_token_value_ = element_name_;
    }
    if (build_token_value_ != element_name_) {
      element_name_error_message_text = context.GetErrorText(
          string("Unexpected element name:") + build_token_value_,
          element_name_);
    }
    result = CallElementFunctions(context, &XmpElementHandler::FinishElement);
    UpdateHandlerActivationLevelsForFinishElement();
    element_name_.clear();
    if (!element_name_stack_.empty()) {
      element_name_ = element_name_stack_.back();
      element_uri_ = GetUri(GetPrefix(element_name_));
      element_name_stack_.pop_back();
    }
  }
  if (!element_name_error_message_text.empty()) {
    result.SetType(DataMatchResult::kError);
    result.SetMessage(Message::kSyntaxError, element_name_error_message_text);
  }
  return result;
}

DataMatchResult XmpDispatchHandler::AttributeName(
    const XmlTokenContext& context) {
  DataMatchResult result = context.GetResult();
  string prefix_error_message_text;
  if (context.BuildTokenValue(&build_token_value_)) {
    attribute_name_ = build_token_value_;
    string prefix = GetPrefix(attribute_name_);
    attribute_uri_ = GetUri(prefix);
    if (!(is_ns_element_ && attribute_name_.find(kXlmNs) == 0)) {  // NOLINT
      if (!prefix.empty()) {
        size_t element_level = element_name_stack_.size();
        auto prefix_pos = prefix_uri_map_.find(prefix);
        if (prefix_pos == prefix_uri_map_.end()) {
          prefix_error_message_text =
              GetUnrecognizedPrefixErrorText(prefix, context);
        } else {
          const auto& uri = prefix_pos->second;
          auto uri_pos = uri_level_map_.find(uri);
          size_t uri_level =
              uri_pos == uri_level_map_.end() ? kMaxSize : uri_pos->second;
          if (element_level < uri_level) {
            prefix_error_message_text =
                GetPrefixInvalidAtThisLocationErrorText(prefix, context);
          }
        }
      }
    }
    result = CallAttributeFunctions(context, &XmpElementHandler::AttributeName);
  }
  if (!prefix_error_message_text.empty()) {
    result.SetMessage(Message::kValueError, prefix_error_message_text);
  }
  return result;
}

DataMatchResult XmpDispatchHandler::AttributeValue(
    const XmlTokenContext& context) {
  string prefix_already_defined_error;
  if (is_ns_element_ && attribute_name_.find(kXlmNs) == 0) {  // NOLINT
    if (context.BuildTokenValue(&build_token_value_)) {
      xmlns_attribute_value_ = build_token_value_;
      prefix_already_defined_error = ProcessXmlnsAttribute(context);
    }
  }
  DataMatchResult result =
      CallAttributeFunctions(context, &XmpElementHandler::AttributeValue);
  if (!prefix_already_defined_error.empty()) {
    result.SetMessage(Message::kValueError, prefix_already_defined_error);
  }
  return result;
}

DataMatchResult XmpDispatchHandler::ElementContent(
    const XmlTokenContext& context) {
  return CallElementFunctions(context, &XmpElementHandler::ElementContent);
}

DataMatchResult XmpDispatchHandler::Cdata(const XmlTokenContext& context) {
  return CallElementFunctions(context, &XmpElementHandler::Cdata);
}

DataMatchResult XmpDispatchHandler::Pi(const XmlTokenContext& context) {
  DataMatchResult result = context.GetResult();
  if (context.BuildTokenValue(&build_token_value_)) {
    const string kXpacket("xpacket");
    if (build_token_value_.substr(0, kXpacket.length()) != kXpacket) {
      result.SetMessage(
          Message::kValueError,
          context.GetErrorText(
              "Unrecognized XMP package wrapper name:" + build_token_value_,
              kXpacket));
    }
  }
  return result;
}

string XmpDispatchHandler::GetUri(const string& prefix) const {
  auto pos = prefix_uri_map_.find(prefix);
  return pos == prefix_uri_map_.end() ? "" : pos->second;
}

DataMatchResult XmpDispatchHandler::CallElementFunctions(
    const XmlTokenContext& context, ElementFunction element_function) {
  size_t element_level = element_name_stack_.size();
  DataMatchResult final_result = context.GetResult();
  for (auto handler_pos = handler_data_map_.begin();
       handler_pos != handler_data_map_.end(); ++handler_pos) {
    const auto& handler_data = handler_pos->second;
    if (element_level >= handler_data.activation_element_level &&
        handler_data.uri_set.count(element_uri_) > 0) {
      DataMatchResult result = (handler_pos->first->*element_function)(
          element_name_stack_, element_name_, context);
      if (result.HasMessage() && !final_result.HasMessage()) {
        final_result.SetMessage(result.GetMessage());
        final_result.SetType(result.GetType());
      }
    }
  }
  return final_result;
}

DataMatchResult XmpDispatchHandler::CallAttributeFunctions(
    const XmlTokenContext& context, AttributeFunction attribute_function) {
  size_t element_level = element_name_stack_.size();
  DataMatchResult final_result = context.GetResult();
  for (auto handler_pos = handler_data_map_.begin();
       handler_pos != handler_data_map_.end(); ++handler_pos) {
    const auto& handler_data = handler_pos->second;
    if ((handler_data.activation_element_level_override ||
         element_level >= handler_data.activation_element_level) &&
        handler_data.uri_set.count(attribute_uri_)) {
      DataMatchResult result = (handler_pos->first->*attribute_function)(
          element_name_stack_, element_name_, attribute_name_, context);
      if (result.HasMessage() && !final_result.HasMessage()) {
        final_result.SetMessage(result.GetMessage());
        final_result.SetType(result.GetType());
      }
    }
  }
  return final_result;
}

string XmpDispatchHandler::ProcessXmlnsAttribute(
    const XmlTokenContext& context) {
  string prefix_already_defined_error;
  string prefix = attribute_name_.substr(6);
  size_t value_length = xmlns_attribute_value_.length();
  // Remove the initial and final quote marks from the value.
  string uri = xmlns_attribute_value_.substr(1, value_length - 2);
  auto prefix_pos = prefix_uri_map_.find(prefix);
  if (prefix_pos == prefix_uri_map_.end()) {
    prefix_uri_map_[prefix] = uri;
    for (auto handler_pos = handler_data_map_.begin();
         handler_pos != handler_data_map_.end(); ++handler_pos) {
      if (handler_pos->second.uri_set.count(uri)) {
        handler_pos->first->SetUriPrefix(uri, prefix);
        handler_pos->second.activation_element_level_override = true;
      }
    }
  } else if (prefix_pos->second != uri) {
    string description = "Xmlns prefix already defined:" + prefix;
    prefix_already_defined_error =
        context.GetErrorText(description, prefix_pos->second);
  }
  size_t uri_level = element_name_stack_.size();
  auto uri_pos = uri_level_map_.find(uri);
  if (uri_pos == uri_level_map_.end()) {
    uri_level_map_[uri] = uri_level;
  } else {
    uri_pos->second = std::min(uri_level, uri_pos->second);
  }
  return prefix_already_defined_error;
}

string XmpDispatchHandler::UpdateHandlerActivationLevelsForStartElement(
    const XmlTokenContext& context) {
  string invalid_xmlns_prefix_error;
  size_t element_level = element_name_stack_.size();
  string prefix = GetPrefix(element_name_);
  if (!prefix.empty()) {
    auto prefix_pos = prefix_uri_map_.find(prefix);
    if (prefix_pos != prefix_uri_map_.end()) {
      const auto& uri = prefix_pos->second;
      auto uri_pos = uri_level_map_.find(uri);
      size_t uri_level =
          uri_pos == uri_level_map_.end() ? kMaxSize : uri_pos->second;
      if (element_level < uri_level) {
        invalid_xmlns_prefix_error =
            GetPrefixInvalidAtThisLocationErrorText(prefix, context);
      } else {
        for (auto handler_pos = handler_data_map_.begin();
             handler_pos != handler_data_map_.end(); ++handler_pos) {
          auto& handler_data = handler_pos->second;
          if (handler_data.uri_set.count(uri)) {
            handler_data.activation_element_level =
                std::min(handler_data.activation_element_level, element_level);
          }
        }
      }
    } else if (!is_ns_element_) {
      invalid_xmlns_prefix_error =
          GetUnrecognizedPrefixErrorText(prefix, context);
    }
  }
  return invalid_xmlns_prefix_error;
}

void XmpDispatchHandler::UpdateHandlerActivationLevelsForFinishElement() {
  size_t element_level = element_name_stack_.size();
  for (auto handler_pos = handler_data_map_.begin();
       handler_pos != handler_data_map_.end(); ++handler_pos) {
    auto& handler_data = handler_pos->second;
    handler_data.activation_element_level_override = false;
    if (handler_data.activation_element_level > element_level) {
      handler_data.activation_element_level = kMaxSize;
    }
  }
  for (auto& uri_and_level : uri_level_map_) {
    size_t& uri_level = uri_and_level.second;
    if (element_level <= uri_level) {
      uri_level = kMaxSize;
    }
  }
}

}  // namespace image_io
}  // namespace photos_editing_formats
