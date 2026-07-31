#include "image_io/xmp/xmp_container_metadata_reader.h"

#include <string>

#include "image_io/base/data_match_result.h"
#include "image_io/xml/xml_token_context.h"
#include "image_io/xmp/xmp_container_metadata.h"
#include "image_io/xmp/xmp_content_handler.h"
#include "image_io/xmp/xmp_errors.h"
#include "image_io/xmp/xmp_helpers.h"
#include "image_io/xmp/xmp_rdf_constants.h"
#include "image_io/xmp/xmp_value.h"

namespace photos_editing_formats {
namespace image_io {

using std::string;
using std::vector;

namespace {

string Name(const string& prefix, const string& suffix) {
  return JoinPrefixAndName(prefix, suffix);
}

}  // namespace

XmpContainerMetadataReader::XmpContainerMetadataReader()
    : directory_element_processed_(false),
      rdf_li_description_context_stack_size_(0) {
  AddUri(kXmpContainerUri);
  AddUri(kXmpContainerItemUri);
  AddUri(kXmpRdfUri);
}

void XmpContainerMetadataReader::SetUriPrefix(const string& uri,
                                              const string& prefix) {
  XmpContent::Type kValue = XmpContent::kValue;
  if (uri == kXmpContainerUri) {
    SetPrefix(kXmpContainerPrefix, prefix);
    AddSupportedName(Name(prefix, kXmpContainerDirectory), kValue);
    AddSupportedName(Name(prefix, kXmpContainerVersion), kValue);
    AddSupportedName(Name(prefix, kXmpContainerItem), kValue);
  } else if (uri == kXmpContainerItemUri) {
    SetPrefix(kXmpContainerItemPrefix, prefix);
    AddSupportedName(Name(prefix, kXmpContainerItemDataUri), kValue);
    AddSupportedName(Name(prefix, kXmpContainerItemMime), kValue);
    AddSupportedName(Name(prefix, kXmpContainerItemLabel), kValue);
    AddSupportedName(Name(prefix, kXmpContainerItemLength), kValue);
    AddSupportedName(Name(prefix, kXmpContainerItemPadding), kValue);
    AddSupportedName(Name(prefix, kXmpContainerItemSemantic), kValue);
    AddSupportedName(Name(prefix, kXmpContainerItem), kValue);
  } else if (uri == kXmpRdfUri) {
    SetPrefix(kXmpRdfPrefix, prefix);
    AddSupportedName(Name(prefix, kXmpRdfResource), kValue);
    AddSupportedName(Name(prefix, kXmpRdfDescription), kValue);
  }
}

DataMatchResult XmpContainerMetadataReader::CheckVersionContext(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  if (!Contains(element_name_stack, kXmpRdfRdfDescription)) {
    return GetNamePathErrorResult(element_name, {}, kXmpRdfRdfDescription,
                                  context);
  }
  return context.GetResult();
}

DataMatchResult XmpContainerMetadataReader::CheckDirectoryContext(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  if (!Contains(element_name_stack, kXmpRdfRdfDescription)) {
    return GetNamePathErrorResult(element_name, {}, kXmpRdfRdfDescription,
                                  context);
  }
  if (directory_element_processed_) {
    return GetDuplicateElementErrorResult(element_name, context);
  }
  directory_element_processed_ = true;
  return context.GetResult();
}

DataMatchResult XmpContainerMetadataReader::CheckItemContext(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  if (rdf_li_description_context_stack_size_ > 0) {
    return context.GetResult();
  }
  container_metadata_.items.emplace_back();
  string directory =
      GetPrefixedName(kXmpContainerPrefix, kXmpContainerDirectory);
  vector<string> path({directory, kXmpRdfRdfSeq, kXmpRdfRdfLi});
  if (!EndsWith(element_name_stack, path)) {
    path.pop_back();
    return GetNamePathErrorResult(element_name, path, kXmpRdfRdfLi, context);
  }
  return context.GetResult();
}

bool XmpContainerMetadataReader::IsRdfLiDescriptionContext(
    const StringVector& element_name_stack, const std::string& element_name) {
  std::string directory =
      GetPrefixedName(kXmpContainerPrefix, kXmpContainerDirectory);
  StringVector path({directory, kXmpRdfRdfSeq, kXmpRdfRdfLi});
  return EndsWith(element_name_stack, path);
}

DataMatchResult XmpContainerMetadataReader::CheckItemChildContext(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  if (rdf_li_description_context_stack_size_ > 0) {
    return context.GetResult();
  }
  string item = GetPrefixedName(kXmpContainerPrefix, kXmpContainerItem);
  if (!EndsWith(element_name_stack, {item})) {
    return GetNamePathErrorResult(element_name, {}, item, context);
  }
  return context.GetResult();
}

DataMatchResult XmpContainerMetadataReader::ProcessElementName(
    const StringVector& element_name_stack, const string& element_name,
    const XmlTokenContext& context) {
  if (IsPrefixedName(element_name, kXmpContainerPrefix, kXmpContainerVersion)) {
    return CheckVersionContext(element_name_stack, element_name, context);
  } else if (IsPrefixedName(element_name, kXmpContainerPrefix,
                            kXmpContainerDirectory)) {
    return CheckDirectoryContext(element_name_stack, element_name, context);
  } else if (IsPrefixedName(element_name, kXmpContainerPrefix,
                            kXmpContainerItem)) {
    return CheckItemContext(element_name_stack, element_name, context);
  } else if (HasPrefix(element_name, kXmpContainerItemPrefix)) {
    return CheckItemChildContext(element_name_stack, element_name, context);
  } else if (IsPrefixedName(element_name, kXmpRdfPrefix, kXmpRdfDescription) &&
             IsRdfLiDescriptionContext(element_name_stack, element_name)) {
    if (rdf_li_description_context_stack_size_ == 0) {
      rdf_li_description_context_stack_size_ = element_name_stack.size();
      container_metadata_.items.emplace_back();
    }
  }
  return context.GetResult();
}

DataMatchResult XmpContainerMetadataReader::ProcessElementContent(
    const StringVector& element_name_stack, const string& element_name,
    const XmpContent& content, const XmlTokenContext& context) {
  if (IsPrefixedName(element_name, kXmpContainerPrefix, kXmpContainerVersion)) {
    return SetXmpValue(content.value, element_name, context,
                       &container_metadata_.version);
  } else if (HasPrefix(element_name, kXmpContainerItemPrefix)) {
    string item = GetPrefixedName(kXmpContainerPrefix, kXmpContainerItem);
    if (!EndsWith(element_name_stack, {item}) &&
        rdf_li_description_context_stack_size_ == 0) {
      return GetNamePathErrorResult(element_name, {}, item, context);
    } else if (IsPrefixedName(element_name, kXmpContainerItemPrefix,
                              kXmpContainerItemDataUri)) {
      return SetXmpValue(content.value, element_name, context,
                         &container_metadata_.items.back().data_uri);
    } else if (IsPrefixedName(element_name, kXmpContainerItemPrefix,
                              kXmpContainerItemMime)) {
      return SetXmpValue(content.value, element_name, context,
                         &container_metadata_.items.back().mime);
    } else if (IsPrefixedName(element_name, kXmpContainerItemPrefix,
                              kXmpContainerItemLabel)) {
      return SetXmpValue(content.value, element_name, context,
                         &container_metadata_.items.back().label);
    } else if (IsPrefixedName(element_name, kXmpContainerItemPrefix,
                              kXmpContainerItemLength)) {
      return SetXmpValue(content.value, element_name, context,
                         &container_metadata_.items.back().length);
    } else if (IsPrefixedName(element_name, kXmpContainerItemPrefix,
                              kXmpContainerItemPadding)) {
      return SetXmpValue(content.value, element_name, context,
                         &container_metadata_.items.back().padding);
    } else if (IsPrefixedName(element_name, kXmpContainerItemPrefix,
                              kXmpContainerItemSemantic)) {
      return SetXmpValue(content.value, element_name, context,
                         &container_metadata_.items.back().semantic);
    }
  } else if (IsPrefixedName(element_name, kXmpRdfPrefix, kXmpRdfDescription) &&
             IsRdfLiDescriptionContext(element_name_stack, element_name)) {
    if (rdf_li_description_context_stack_size_ == element_name_stack.size()) {
      rdf_li_description_context_stack_size_ = 0;
    }
  } else if (IsPrefixedName(element_name, kXmpRdfPrefix, kXmpRdfResource)) {
    if (rdf_li_description_context_stack_size_ > 0) {
      std::string item_uri_name = Name(kXmpContainerUri, kXmpContainerItem);
      if (content.value != item_uri_name) {
        return GetInvalidValueErrorResult(element_name, {item_uri_name},
                                          context);
      }
    }
  }
  return context.GetResult();
}

DataMatchResult XmpContainerMetadataReader::ProcessAttributeName(
    const StringVector& element_name_stack, const string& element_name,
    const string& attribute_name, const XmlTokenContext& context) {
  if (HasPrefix(attribute_name, kXmpContainerPrefix)) {
    if (IsPrefixedName(attribute_name, kXmpContainerPrefix,
                       kXmpContainerDirectory)) {
      return GetCantBeAnAttributeErrorResult(attribute_name, context);
    } else if (element_name != kXmpRdfRdfDescription) {
      return GetNamePathErrorResult(attribute_name, {}, kXmpRdfRdfDescription,
                                    context);
    }
  } else if (HasPrefix(attribute_name, kXmpContainerItemPrefix)) {
    string item = GetPrefixedName(kXmpContainerPrefix, kXmpContainerItem);
    if (element_name != item) {
      return GetNamePathErrorResult(attribute_name, {}, item, context);
    }
  }
  return context.GetResult();
}

}  // namespace image_io
}  // namespace photos_editing_formats
