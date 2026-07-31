#include "image_io/xmp/xmp_writer.h"

#include <iomanip>
#include <string>

#include "image_io/xmp/xmp_helpers.h"
#include "image_io/xmp/xmp_meta_constants.h"
#include "image_io/xmp/xmp_rdf_constants.h"

namespace photos_editing_formats {
namespace image_io {

namespace {

std::string Name(const std::string& prefix, const std::string& name) {
  return JoinPrefixAndName(prefix, name);
}

}  // namespace

size_t XmpWriter::StartWritingRdfDescriptionElement(XmlWriter* xml_writer) {
  size_t depth =
      xml_writer->StartWritingElement(Name(kXmpRdfPrefix, kXmpRdfDescription));
  xml_writer->WriteAttributeNameAndValue(Name(kXmpRdfPrefix, "about"), "");
  return depth;
}

XmpWriter::XmpWriter(std::ostream& os) : xml_writer_(os) {}

void XmpWriter::Write() {
  StartWritingXmpmetaElement();
  StartWritingRdfRdfElement();
  StartWritingRdfDescriptionElement(&xml_writer_);
  for (auto* writer_source : writer_sources_) {
    writer_source->WriteNamespaces(&xml_writer_);
  }
  for (auto* writer_source : writer_sources_) {
    writer_source->WriteAttributeNamesAndValues(&xml_writer_);
  }
  for (auto* writer_source : writer_sources_) {
    size_t start_depth = xml_writer_.GetElementDepth();
    writer_source->WriteElements(&xml_writer_);
    size_t current_depth = xml_writer_.GetElementDepth();
    if (current_depth > start_depth) {
      xml_writer_.FinishWritingElementsToDepth(start_depth);
    }
  }
  xml_writer_.FinishWriting();
}

void XmpWriter::StartWritingXmpmetaElement() {
  xml_writer_.StartWritingElement(Name(kXmpMetaPrefix, kXmpMeta));
  xml_writer_.WriteXmlns(kXmpMetaPrefix, kXmpMetaUri);
  xml_writer_.WriteAttributeNameAndValue(Name(kXmpMetaPrefix, kXmpMetaXmptk),
                                         kXmpMetaXmptkValue);
}

void XmpWriter::StartWritingRdfRdfElement() {
  xml_writer_.StartWritingElement(Name(kXmpRdfPrefix, kXmpRdfRdf));
  xml_writer_.WriteXmlns(kXmpRdfPrefix, kXmpRdfUri);
}

}  // namespace image_io
}  // namespace photos_editing_formats
