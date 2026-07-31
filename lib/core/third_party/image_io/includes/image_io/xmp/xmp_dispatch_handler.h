#ifndef IMAGE_IO_XMP_XMP_DISPATCH_HANDLER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_DISPATCH_HANDLER_H_  // NOLINT

#include <map>
#include <set>
#include <string>
#include <vector>

#include "image_io/xml/xml_handler.h"
#include "image_io/xmp/xmp_element_handler.h"

namespace photos_editing_formats {
namespace image_io {

/// This subclass of XmlHandler maintains the state needed to make interpreting
/// the contents of XMP elements much simpler. Client code that is interested in
/// a given set of XMP attributes and elements can express the processing in a
/// subclass of XmpElementHandler and pass it to an XmpDispatchHandler before
/// starting the XMP reading process. When the dispatch handler encounters an
/// attribute or element in the namespace of an interested element handler, it
/// passes the information on to the element handler. Element handlers are not
/// called for attributes or elements that belong to namespaces they are not
/// interested in.
///
/// Two assumptions are made by this class to simplify XMP processing:
/// 1. Once an xmlns prefix is associated with a uri, it cannot be changed.
/// 2. The prefixes associated with the top possible top level elements --
///    xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" and
///    xlmns:x="adobe:ns:meta/" -- are hard coded and cannot be changed.
class XmpDispatchHandler : public XmlHandler {
 public:
  /// @return The xmlns uri for the "x" prefix.
  static std::string GetUriForPrefixX();

  /// @return The xmlns uri for the "rdf" prefix.
  static std::string GetUriForPrefixRdf();

  /// @return The xmlns prefix for RDF elements and attributes.
  static std::string GetPrefixRdf();

  XmpDispatchHandler();

  /// @param handler An element handler to added to this dispatch handler.
  void AddElementHandler(XmpElementHandler* handler);

  /// @param handler The element handler to be removed from this dispatch
  /// handler.
  void RemoveElementHandler(XmpElementHandler* handler);

  /// Calls the FinishParse() function of all element handlers.
  /// @return Whether all the element handlers' FinishParse() return true.
  bool FinishParse();

  /// Overrides where all the magic happens.
  DataMatchResult StartElement(const XmlTokenContext& context) override;
  DataMatchResult FinishElement(const XmlTokenContext& context) override;
  DataMatchResult AttributeName(const XmlTokenContext& context) override;
  DataMatchResult AttributeValue(const XmlTokenContext& context) override;
  DataMatchResult ElementContent(const XmlTokenContext& context) override;
  DataMatchResult Cdata(const XmlTokenContext& context) override;
  DataMatchResult Pi(const XmlTokenContext& context) override;

 private:
  /// A simplifying typedef
  using StringVector = std::vector<std::string>;

  /// A typedef for a pointer to XmpElementHandler member function that deals
  /// with element names and contents.
  using ElementFunction = DataMatchResult (XmpElementHandler::*)(
      const StringVector&, const std::string&, const XmlTokenContext& context);

  /// A typedef for a pointer to XmpElementHandler member function that deals
  /// with attribute names and values.
  using AttributeFunction = DataMatchResult (XmpElementHandler::*)(
      const StringVector&, const std::string&, const std::string&,
      const XmlTokenContext& context);

  /// The internal data associated with each element handler.
  struct HandlerData {
    HandlerData();
    /// The set of uris that the element handler is interested in.
    std::set<std::string> uri_set;

    /// The size of the element name stack at which the handler is considered
    /// "active" - i.e., its handler functions called.
    size_t activation_element_level;

    /// Whether the attribute name/value functions of the element handler
    /// should be called even though the handler is not in an active state
    /// because of the element name stack size. This occurs when an attribute
    /// name/value appear in the same element that defines the prefix/uri.
    bool activation_element_level_override;
  };

  /// @param prefix The prefix for which the uri is desired.
  /// @return A uri value or an empty string if the prefix is not found.
  std::string GetUri(const std::string& prefix) const;

  /// Calls the element functions of the element handler if the handler is in an
  /// active state and it is intersested in the element's uri.
  /// @param context The parsing context for values and error messages.
  /// @param element_function The XmpElementHandler function to call.
  /// @return The result from the context with an optional error message.
  DataMatchResult CallElementFunctions(const XmlTokenContext& context,
                                       ElementFunction element_function);

  /// Calls the attribute functions of the element handler if the handler is in
  /// an active state and it is intersested in the element's uri.
  /// @param context The parsing context for values and error messages.
  /// @param element_function The XmpElementHandler function to call.
  /// @return The result from the context with an optional error message.
  DataMatchResult CallAttributeFunctions(const XmlTokenContext& context,
                                         AttributeFunction);

  /// Examines the current attribute name (that is known to start with xmlns:)
  /// and attribute value and updates the prefix url and url level maps.
  /// This function will create an error message if the xlmns prefix is already
  /// defined and has a different uri value.
  /// @param context The parsing context for generating error messages.
  /// @return An error message or an empty string if no errors.
  std::string ProcessXmlnsAttribute(const XmlTokenContext& context);

  /// Examines the current element's name, extracts the xlmns prefix and finds
  /// the related uri. If there is no uri, it returns an error message. It then
  /// verifies that the uri is valid at this location, and if not returns an
  /// error message. Finally, for each handler interested in the uri, it updates
  /// its activation level controlling when the handler's functions are called.
  /// @param context The parsing context for generating error messages.
  /// @return An error message or an empty string if no errors.
  std::string UpdateHandlerActivationLevelsForStartElement(
      const XmlTokenContext& context);

  /// Updates each handler's activation level based on the current size of the
  /// element name stack.
  void UpdateHandlerActivationLevelsForFinishElement();

  /// The set of element names that can contain xmlns type attributes.
  std::set<std::string> ns_element_name_set_;

  /// The map of xmlns prefix to its uri value.
  std::map<std::string, std::string> prefix_uri_map_;

  /// The map of xmlns uri to the element level where it was first defined.
  std::map<std::string, size_t> uri_level_map_;

  /// The map of handler and handler data used to control when the handler
  /// functions are called.
  std::map<XmpElementHandler*, HandlerData> handler_data_map_;

  /// A stack holding the names of the elements in the "XML tree" being parsed.
  /// It does not include the current element the name of which is stored in the
  /// element_name_ member variable.
  StringVector element_name_stack_;

  /// A string to hold the current token being built. When the token value is
  /// complete the value is copied to the element_name_, attribute_name_ or
  /// attribute_value_ member variables.
  std::string build_token_value_;

  /// The name of the current element.
  std::string element_name_;

  /// The uri associated with the current element name's prefix.
  std::string element_uri_;

  /// The name of the current attribute.
  std::string attribute_name_;

  /// The uri associated with the current attribute name's prefix.
  std::string attribute_uri_;

  /// The value of an xmlns type attribute.
  std::string xmlns_attribute_value_;

  /// Whether the current element is one that define xlmns type attributes.
  bool is_ns_element_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_DISPATCH_HANDLER_H_  // NOLINT
