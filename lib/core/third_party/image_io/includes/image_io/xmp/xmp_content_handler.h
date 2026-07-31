#ifndef IMAGE_IO_XMP_XMP_CONTENT_HANDLER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_CONTENT_HANDLER_H_  // NOLINT

#include <map>
#include <set>
#include <string>
#include <vector>

#include "image_io/base/data_range.h"
#include "image_io/xmp/xmp_element_handler.h"

namespace photos_editing_formats {
namespace image_io {

/// A container for a string value and a vector of ranges used to represent
/// both an element's content and an attribute's value.
struct XmpContent {
  /// The type of information that is contained in this object.
  enum Type { kNone = 0, kValue = 1, kRange = 2, kValueAndRange = 3 };

  /// The default constructor creates an object that will not contain nothing.
  XmpContent() : type(kNone) {}

  /// @param v The value to assign to the object.
  /// @param r The range to assign to the object.
  /// @param t The type of object.
  XmpContent(const std::string& v, const std::vector<DataRange>& r, Type t)
      : value(v), ranges(r), type(t) {}

  /// @return Whether the object has a valid value field.
  bool HasValue() const { return (type & kValue) != 0; }

  /// @return Whether the object has a valid ranges field.
  bool HasRange() const { return (type & kRange) != 0; }

  /// The equality operators;
  bool operator!=(const XmpContent& rhs) const { return !(*this == rhs); }
  bool operator==(const XmpContent& rhs) const {
    return type == rhs.type && value == rhs.value && ranges == rhs.ranges;
  }

  std::string value;
  std::vector<DataRange> ranges;
  Type type;
};

/// A type of XmpElementHandler that focuses on the content of elements and the
/// values of attributes. This handler can be used in situations where those
/// values can be assumed to be of a limited size so that their string values
/// can be obtained, allowing the handler functions to be simplified and
/// dedicated to checking the name contexts and correctly obtaining, converting
/// and assigning values to other data members. To process values of names use
/// the XmpContent::kValue type when calling the AddSupportedName() function.
///
/// The handler can also be used in for cases where the values (particularly the
/// attribute values) are too large to represent conveniently in memory. In this
/// case, a vector of image_io::DataRange can be obtained and saved for later.
/// To process values of names use the XmpContent::kRange type when calling
/// the AddSupportedName() function.
///
/// The element and attribute names used with this class are "prefixed names" -
/// i.e., of the form "prefix:name". Since the true XMP/XML prefix is typically
/// not known until the xlmns attribute is read, many of the name and prefix
/// functions work with a prefix key, which is typically the default prefix
/// value. Subclasses should call the SetPrefix() function from their override
/// of the XmpElementHander::SetUriPrefix() function.
///
/// The Process* virtual functions that this class defines are only called for
/// supported element and attribute names. Subclasses need to add their
/// prefixed names to the supported names set. Subclasses can also add prefixed
/// names to a deprecated name set. When elements and/or attributes with those
/// names are encountered, the handler will automatcially return a result with
/// an appropriate error message.
class XmpContentHandler : public XmpElementHandler {
 public:
  XmpContentHandler();

  /// @param prefix_key The prefix value to use as a key in the prefixes map.
  /// This is usually the default prefix value for the name.
  /// @param prefix The actual prefix being used in the XMP's XML.
  void SetPrefix(const std::string& prefix_key, const std::string& prefix) {
    prefixes_[prefix_key] = prefix;
  }

  /// @param prefix_key The value to use as a key in the prefixes map.
  /// @return The value in the prefixes map for the key, or the key itself if
  /// the map does not contain the key.
  std::string GetPrefix(const std::string& prefix_key) const {
    auto pos = prefixes_.find(prefix_key);
    return pos == prefixes_.end() ? prefix_key : pos->second;
  }

  /// @param prefix_key The prefix key for the prefix part of the returned name.
  /// @param name The name part of the returned prefixed name.
  /// @return The prefixed name of the form prefix:name, where prefix is found
  /// by using the prefix_key as the lookup key in the prefixes map. If the map
  /// does not have a value for the key, the prefix_key is used for the prefix.
  std::string GetPrefixedName(const std::string& prefix_key,
                              const std::string& name) const;

  /// @param prefixed_name The name to check the prefix of.
  /// @param prefix_key The prefix key used to obtain the prefix to use. If the
  /// prefixes map does not have the key, the prefix key is used as the prefix.
  /// @return Whether the prefixed name starts with the prefix.
  bool HasPrefix(const std::string& prefixed_name,
                 const std::string& prefix_key) const;

  /// @param prefixed_name The name to compare to the prefix and name.
  /// @parma prefix_key The prefix key used to obtain the prefix to use. If the
  /// prefixes map does not have the key, the prefix key is used as the prefix.
  /// @param name The suffix part of the name to use.
  /// @return Whether the prefixed name matches tha specified by the prefix key
  /// and name.
  bool IsPrefixedName(const std::string& prefixed_name,
                      const std::string& prefix_key,
                      const std::string& name) const;

  /// @param name The prefixed name that is to be supported.
  /// @param type The type of support: string value or ranges or both.
  void AddSupportedName(const std::string& name, XmpContent::Type type);

  /// @param name The prefixed name to obtain the support type of.
  /// @return The support type or kNone if the name is not supported.
  XmpContent::Type GetSupportedNameType(const std::string& name) const;

  /// @param name The prefixed name of the name that is to be deprecated.
  void AddDeprecatedName(const std::string& name) {
    deprecated_names_.insert(name);
  }

  /// @param name The prefixed name of the potentially deprecated name.
  /// @return Whether the name is deprecated.
  bool IsDeprecatedName(const std::string& name) const {
    return deprecated_names_.count(name) != 0;
  }

  /// This function is called by the StartElement() function for element names
  /// that are contained in the supported name set.
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element being finished.
  /// @param context The token context that can be used for error messages.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult ProcessElementName(
      const StringVector& element_name_stack, const std::string& element_name,
      const XmlTokenContext& context);

  /// This function is called by the FinishElement() function for supported
  /// element names.
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element being finished.
  /// @param element_content The content value and/or range of the element.
  /// @param context The token context that can be used for error messages.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult ProcessElementContent(
      const StringVector& element_name_stack, const std::string& element_name,
      const XmpContent& element_content, const XmlTokenContext& context);

  /// This function is called by the AttributeName() function for attribute
  /// names that are contained in the supported name set.
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element containing the attribute.
  /// @param context The token context that can be used for error messages.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult ProcessAttributeName(
      const StringVector& element_name_stack, const std::string& element_name,
      const std::string& attribute_name, const XmlTokenContext& context);

  /// The default implementation of this function is to treat the attribute
  /// name and value as a "pseudo child node" and call ProcessElementContent().
  /// This allows subclasses to override only the ProcessElementContent function
  /// and do the checking and assignment of values there.
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element containing the attribute.
  /// @param attribute_name The name of the attribute being processed.
  /// @param attribute_content The string value and/or ranges of the attribute
  /// value.
  /// @param context The token context that can be used for error messages.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult ProcessAttributeValue(
      const StringVector& element_name_stack, const std::string& element_name,
      const std::string& attribute_name, const XmpContent& attribute_content,
      const XmlTokenContext& context);

  /// These XmpElementHandler functions have implementations that cannot be
  /// overridden by subclasses. They call the other Process* virtual functions
  /// defined by this class: ProcessElementName(), ProcessElementContent(),
  /// ProcessAttributeName() and ProcessAttributeValue(), all of which can be
  /// overridden.
  DataMatchResult StartElement(const StringVector& element_name_stack,
                               const std::string& element_name,
                               const XmlTokenContext& context) final;
  DataMatchResult FinishElement(const StringVector& element_name_stack,
                                const std::string& element_name,
                                const XmlTokenContext& context) final;
  DataMatchResult AttributeName(const StringVector& element_name_stack,
                                const std::string& element_name,
                                const std::string& attribute_name,
                                const XmlTokenContext& context) final;
  DataMatchResult AttributeValue(const StringVector& element_name_stack,
                                 const std::string& element_name,
                                 const std::string& attribute_name,
                                 const XmlTokenContext& context) final;
  DataMatchResult ElementContent(const StringVector& element_name_stack,
                                 const std::string& element_name,
                                 const XmlTokenContext& context) final;

 private:
  std::map<std::string, std::string> prefixes_;
  std::map<std::string, XmpContent::Type> supported_name_types_;
  std::set<std::string> deprecated_names_;
  std::vector<XmpContent> element_contents_;
  XmpContent attribute_value_range_;
  bool building_attribute_value_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_CONTENT_HANDLER_H_  // NOLINT
