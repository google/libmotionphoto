#ifndef IMAGE_IO_XMP_XMP_WRITER_SOURCE_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_WRITER_SOURCE_H_  // NOLINT

#include <string>

#include "image_io/xml/xml_writer.h"
#include "image_io/xmp/xmp_value.h"

namespace photos_editing_formats {
namespace image_io {

class XmpWriter;

/// A class that collaborates with an XmpWriter to write the metadata for a
/// specific component. Metadata implementors subclass this interface and
/// write their namespace, attribute and element data when called upon.
///
/// The element and attribute names that are passed to the XmlWriter are assumed
/// to have the form "prefix:suffix" where prefix is one of the xmlns prefixes
/// that was added.
///
/// When writing element content and attribute values no XML escaping of any
/// kind is done. If client code needs to do that, it should do it before
/// calling the XmlWriter functions.
class XmpWriterSource {
 public:
  virtual ~XmpWriterSource() = default;

  /// The XmpWriter calls this function when a source is add to it. Writer
  /// sources can override this implementation (which does nothing) and perhaps
  /// add other sources to the xmp writer allowing the object that is being
  /// written to contain data members by composition that have their own writer
  /// sources.
  /// @param xmp_writer The writer that this source was added to.
  virtual void StartWrite(XmpWriter* xmp_writer) {}

  /// This function is called when the XmpWriter has started an rdf:Description
  /// type element. Subclass code can write their Xml namespace prefixes and URI
  /// values in this function.
  /// @param writer The XmlWriter to write to.
  virtual void WriteNamespaces(XmlWriter* writer) {}

  /// This function is called when the XmpWriter has started an rdf:Description
  /// type element, and after all the writer sources have written their Xml
  /// namespaces. Subclass code can write their attribute values to the
  /// rdf:Description element.
  /// @param writer The XmlWriter to write to.
  virtual void WriteAttributeNamesAndValues(XmlWriter* writer) {}

  /// This function is called when the XmpWriter has started an rdf:Description
  /// type element, and after all the writer sources have written their
  /// attribute names and values. Subclass code can write their element(s) to
  /// the xml writer.
  /// @param writer The XmlWriter to write to.
  virtual void WriteElements(XmlWriter* writer) {}
};

/// A helper function that writes the attribute name and value if the value is
/// valid, otherwise does nothing.
/// @param name The attribute name to write.
/// @param value The value to write if its IsValid() function returns true.
/// @return Whether the attribute name and value were written.
template <class T>
bool MaybeWriteAttributeNameAndValue(const std::string& name,
                                     const XmpValue<T>& value,
                                     XmlWriter* writer) {
  if (value.IsValid()) {
    writer->WriteAttributeNameAndValue(name, value.GetValue());
    return true;
  }
  return false;
}

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_WRITER_SOURCE_H_  // NOLINT
