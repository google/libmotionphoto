#include "image_io/xmp/xmp_element_handler.h"

#include <algorithm>
#include <string>

#include "image_io/xmp/xmp_errors.h"

namespace photos_editing_formats {
namespace image_io {

bool XmpElementHandler::EndsWith(const StringVector& path,
                                 const StringVector& tail) {
  if (tail.empty()) {
    return true;
  } else if (tail.size() > path.size()) {
    return false;
  }
  for (size_t delta = 1; delta <= tail.size(); ++delta) {
    if (path[path.size() - delta] != tail[tail.size() - delta]) {
      return false;
    }
  }
  return true;
}

bool XmpElementHandler::Contains(const StringVector& path,
                                 const std::string& part) {
  auto pos = std::find(path.begin(), path.end(), part);
  return pos != path.end();
}

XmpElementHandler::XmpElementHandler() : cdata_error_(true) {}

void XmpElementHandler::AddUri(const std::string& uri) { uris_.push_back(uri); }

const XmpElementHandler::StringVector& XmpElementHandler::GetUris() const {
  return uris_;
}

void XmpElementHandler::SetUriPrefix(const std::string& uri,
                                     const std::string& prefix) {}

DataMatchResult XmpElementHandler::StartElement(
    const StringVector& element_name_stack, const std::string& element_name,
    const XmlTokenContext& context) {
  return context.GetResult();
}

DataMatchResult XmpElementHandler::FinishElement(
    const StringVector& element_name_stack, const std::string& element_name,
    const XmlTokenContext& context) {
  return context.GetResult();
}

DataMatchResult XmpElementHandler::AttributeName(
    const StringVector& element_name_stack, const std::string& element_name,
    const std::string& attribute_name, const XmlTokenContext& context) {
  return context.GetResult();
}

DataMatchResult XmpElementHandler::AttributeValue(
    const StringVector& element_name_stack, const std::string& element_name,
    const std::string& attribute_name, const XmlTokenContext& context) {
  return context.GetResult();
}

DataMatchResult XmpElementHandler::ElementContent(
    const StringVector& element_name_stack, const std::string& element_name,
    const XmlTokenContext& context) {
  return context.GetResult();
}

DataMatchResult XmpElementHandler::Cdata(const StringVector& element_name_stack,
                                         const std::string& element_name,
                                         const XmlTokenContext& context) {
  if (IsCdataError()) {
    return GetCdataNotSupportedErrorResult(context);
  }
  return context.GetResult();
}

}  // namespace image_io
}  // namespace photos_editing_formats
