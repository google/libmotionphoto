#ifndef IMAGE_IO_XMP_XMP_ELEMENT_HANDLER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_ELEMENT_HANDLER_H_  // NOLINT

#include <set>
#include <string>
#include <vector>

#include "image_io/base/data_match_result.h"
#include "image_io/xml/xml_token_context.h"

namespace photos_editing_formats {
namespace image_io {

class XmpDispatchHandler;

/// The handler that is called by XmpDispatchHandler as it process the XML data
/// provided to it by an XmlReader and XmlRules. Element handlers like this are
/// added to dispatch handlers before the reading process takes place. When
/// an element handler is added to a dispatch handler, the latter queries the
/// former for the list of xmlns uris that it is interested in. Only elements
/// and attributes that have prefixes refering to those uris are passed to the
/// element handler.
///
/// The dispatch handler builds and maintains a minimal set of data to make
/// understanding the element and attribute context (i.e., the element node
/// position in the XML tree being parsed) and passes that context to each of
/// this handler's member functions as a string vector of element names. The
/// call of this handler's member functions are delayed until the full
/// element/attribute name is known. However for element context, cdata, and
/// attribute values, the token context must be used by subclasses to build the
/// respective values, or passing the information on in a memory sensitive way.
///
/// The dispatch handler provides the StartElement(), FinishElement() and
/// AttributeName() functions with the fully formed element and attribute name,
/// elimanting the need for the element handler to obtain them from the context.
/// (In fact attempts to do so may fail, since the element handler functions
/// are not called until the name token inidates that the name is complete).
/// For the AttributeValue(), ElementContent() and Cdata() functions, the
/// element handler must obtain the attribute value, element content value or
/// Cdata value from the context, using the context's BuildTokenValue() function
/// or GetTokenRange() and other functions of DataContext. Care should be taken
/// to ensure that the memory requirements of the string built by the
/// BuildTokenValue() function are not execessive.
///
/// Regarding attribute names and values - XML allows the information to be
/// represented instead as a child element and element content. I.e., instead of
///   <element name="value" />
/// the XML could written
///   <element> <name>value</name> </element>
/// Consequently, any processing that is done in the AttributeName() and
/// AttributeValue() functions should also be performed in the StartElement()
/// and ElementContent() functions.
///
/// Regarding the xlmns prefixes for element and attribute names -- it is the
/// xmlns uri that is standard, not the prefix. Consequently, the dispatch
/// handler provides the prefix to be associated with the uri when the xlmns
/// for the prefix and uri is processed, by calling the element handler's
/// SetUriPrefix() function. Element handlers should store the given prefix or
/// build the set of element and attribute names they need.
///
/// Each of the functions can return a DataMatchResult value. This value is
/// typically just obtained from the XmlTokenContext via a call to GetResult().
/// If there is a problem with the name or value, then the element handler
/// can use the context's GetErrorText() function to add an error message to
/// the returned result. In most cases, the message type should indicate a
/// value error or some other type of error, but not a syntax or internal error.
/// (Syntax and internal error will cause the parsing of the XML source to be
/// terminated). Here example code of how to create a result with an error:
///
/// DataMatchResult StartElement(const StringVector& element_name_stack,
///                              const std::string& element_name,
///                              const XmlTokenContext& context) {
///   DataMatchResult result = context.GetResult();
///   if (some error condition exists) {
///     std::string description = "some description of the error";
///     result.SetMessage(Message::kValueError,
///                       context.GetErrorText(description, "expected value"));
///   }
///   return result;
/// }
///
/// Note that if multiple element handlers are added to a single dispatch
/// handler and more than one returns a result with an error message, only the
/// first one is returned to the XmlReader and reported to the user.
class XmpElementHandler {
 public:
  /// A simplifying typedef.
  using StringVector = std::vector<std::string>;

  /// @param path Typically the element names from the root element.
  /// @param tail A vector with the final elements in a path.
  /// @return Whether the path vector ends with the tail vector.
  static bool EndsWith(const StringVector& path, const StringVector& tail);

  /// @param path Typically the element names from the root element.
  /// @param part An entry in the path..
  /// @return Whether the path vector contains the part.
  static bool Contains(const StringVector& path, const std::string& part);

  XmpElementHandler();
  virtual ~XmpElementHandler() = default;

  /// @param uri An xmlns uri that the element handler is interested in.
  void AddUri(const std::string& uri);

  /// Dispatch handlers call this function when the element handler is added.
  /// @return A vector of xmlns uris that the elemetn handler is interested in.
  const StringVector& GetUris() const;

  /// Dispatch handlers call this function when the element handler is added to
  /// it. Element handlers can override this implementation (which does nothing)
  /// and perhaps add other handlers to the dispatch handler allowing the object
  /// that is being read to contain data members by composition that have their
  /// own reader handler.
  /// @param dispatch_handler The dispatch handler that this element handler was
  /// just added to via tje XmpDispatchHandler::AddElementHandler() function.
  virtual void StartParse(XmpDispatchHandler* dispatch_handler) {}

  /// Dispatch handlers call this function from their FinishParse() function
  /// to allow element handlers to know that parsing is done. The return codes
  /// of all handlers are anded together for the dispatch handler return code.
  /// @return Whether the parsing was successfully from this element handler's
  /// perspective.
  virtual bool FinishParse() { return true; }

  /// Dispatch handlers call this function when they encounter an attribute with
  /// the xmlns:prefix="uri" type syntax. Element handlers can override this
  /// implementation (which does nothing) to build element and attribute names
  /// for later use in the other member functions.
  /// @param uri The xmlns prefix attribute value
  /// @param prefix The prefix part of the attribute name.
  virtual void SetUriPrefix(const std::string& uri, const std::string& prefix);

  /// This function is called by a dispatch handler when starting an XML
  /// element. Once started, any of the other handler functions may be called.
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element being started.
  /// @param context The token context used to specify the element name.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult StartElement(const StringVector& element_name_stack,
                                       const std::string& element_name,
                                       const XmlTokenContext& context);

  /// This function is called by a dispatch handler when finishing an element.
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element being finished.
  /// @param context The token context used to specify the element name.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult FinishElement(const StringVector& element_name_stack,
                                        const std::string& element_name,
                                        const XmlTokenContext& context);

  /// This function is called by a dispatch handler for an attribute name.
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element containing the attribute.
  /// @param attribute_name The name of the attribute being defined.
  /// @param context The token context used to specify the attribute name.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult AttributeName(const StringVector& element_name_stack,
                                        const std::string& element_name,
                                        const std::string& attribute_name,
                                        const XmlTokenContext& context);

  /// This function is called by a dispatch handler for an attribute value.
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element containing the attribute.
  /// @param attribute_name The name of the attribute being defined.
  /// @param context The token context used to specify the attribute value.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult AttributeValue(const StringVector& element_name_stack,
                                         const std::string& element_name,
                                         const std::string& attribute_name,
                                         const XmlTokenContext& context);

  /// This function is called by a dispatch handler for element content values.
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element of the content.
  /// @param context The token context used to specify the content value.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult ElementContent(const StringVector& element_name_stack,
                                         const std::string& element_name,
                                         const XmlTokenContext& context);

  /// This function is called by a dispatch handler for element Cdata values.
  /// Note: the XMP standard does not recommend using the Cdata syntax -
  /// consequently this function issues an error if the IsCdataError() function
  /// returns a true value (the default case).
  /// @param element_name_stack The names of the elements from the XML root
  /// node to the one that contains the element name passed to this function.
  /// It does not include the element named by element_name parameter.
  /// @param element_name The name of the element of the content.
  /// @param context The token context used to specify the Cdata value.
  /// @return The match result from the context, or one that is modified to
  /// contain an error message if needed.
  virtual DataMatchResult Cdata(const StringVector& element_name_stack,
                                const std::string& element_name,
                                const XmlTokenContext& context);

  /// @return Whether XML CDATA syntax is to be considered an error by the
  /// Cdata() function, above. If so, the function will return a result that
  /// contains an error message. The default value for this flag is true.
  const bool IsCdataError() const { return cdata_error_; }

  /// @param The new value for whether XML CDATA syntax is considered an error.
  void SetCdataError(bool cdata_error) { cdata_error_ = cdata_error; }

 private:
  /// The uris that the element handler is iterested in.
  StringVector uris_;

  /// Whether the XML CDATA syntax will result in an error.
  bool cdata_error_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_ELEMENT_HANDLER_H_  // NOLINT
