#ifndef IMAGE_IO_XMP_XMP_WRITER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_WRITER_H_  // NOLINT

#include <ostream>
#include <vector>

#include "image_io/xml/xml_writer.h"
#include "image_io/xmp/xmp_writer_source.h"

namespace photos_editing_formats {
namespace image_io {

/// An writer that manages an XmlWriter to produce XMP/XML output from one or
/// more writer sources.
class XmpWriter {
 public:
  /// Starts writing a new rdf:Description element.
  /// @return The element depth of the XmlWriter when this function is called.
  /// Client code can use this with XmlWriter::FinishWritingElementsToDepth()
  /// to finish off the elements added after the rdf:Description element.
  static size_t StartWritingRdfDescriptionElement(XmlWriter* xml_writer);

  /// @param os The stream to which the XMP/XML is written.
  explicit XmpWriter(std::ostream& os);

  /// @return The XmlWriter that is doing the heavy lifting.
  XmlWriter* GetXmlWriter() { return &xml_writer_; }

  /// Adds the writer source to the vector of such sources. The member functions
  /// of these sources are called from the Write() function.
  /// @param writer_source The source of the information that is written to the
  /// XmlWriter.
  void AddWriterSource(XmpWriterSource* writer_source) {
    writer_sources_.push_back(writer_source);
    writer_source->StartWrite(this);
  }

  /// Call this function to write the XML to the ostream.
  void Write();

  /// @return The number of elements written.
  size_t GetElementCount() const { return xml_writer_.GetElementCount(); }

  /// @return The quote mark used when writing attribute values. The default
  /// value set up by the constructor is the double quote (").
  char GetQuoteMark() const { return xml_writer_.GetQuoteMark(); }

  /// @param quote_park The new quote mark to use when writing attribute values.
  void SetQuoteMark(char quote_mark) { xml_writer_.SetQuoteMark(quote_mark); }

 private:
  /// Starts writing a new x:xmpmeta element.
  void StartWritingXmpmetaElement();

  /// Starts writing a new rdf:RDF element.
  void StartWritingRdfRdfElement();

  /// The writer sources.
  std::vector<XmpWriterSource*> writer_sources_;

  /// The xml writer.
  XmlWriter xml_writer_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_WRITER_H_  // NOLINT
