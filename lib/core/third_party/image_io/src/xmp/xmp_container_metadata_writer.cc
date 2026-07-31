#include "image_io/xmp/xmp_container_metadata_writer.h"

#include <cstddef>
#include <string>

#include "image_io/xml/xml_writer.h"
#include "image_io/xmp/xmp_container_metadata.h"
#include "image_io/xmp/xmp_helpers.h"
#include "image_io/xmp/xmp_rdf_constants.h"
#include "image_io/xmp/xmp_writer_source.h"

namespace photos_editing_formats {
namespace image_io {

using std::string;

namespace {

string Name(const string& prefix, const string& suffix) {
  return JoinPrefixAndName(prefix, suffix);
}

}  // namespace

XmpContainerMetadataWriter::XmpContainerMetadataWriter(
    const XmpContainerMetadata& metadata)
    : metadata_(metadata) {}

void XmpContainerMetadataWriter::WriteNamespaces(XmlWriter* writer) {
  writer->WriteXmlns(kXmpContainerPrefix, kXmpContainerUri);
  writer->WriteXmlns(kXmpContainerItemPrefix, kXmpContainerItemUri);
}

void XmpContainerMetadataWriter::WriteAttributeNamesAndValues(
    XmlWriter* writer) {
  const string kVersion = Name(kXmpContainerPrefix, kXmpContainerVersion);
  if (!MaybeWriteAttributeNameAndValue(kVersion, metadata_.version, writer)) {
    writer->WriteAttributeNameAndValue(kVersion, kXmpContainerVersionCurrent);
  }
}

void XmpContainerMetadataWriter::WriteElements(XmlWriter* writer) {
  if (metadata_.items.empty()) {
    return;
  }
  // clang-format off
  const string kDirectory(Name(kXmpContainerPrefix, kXmpContainerDirectory));
  const string kSeq(Name(kXmpRdfPrefix, kXmpRdfSeq));
  const string kLi(Name(kXmpRdfPrefix, kXmpRdfLi));
  const string kItem(Name(kXmpContainerPrefix, kXmpContainerItem));
  const string kLabel(Name(kXmpContainerItemPrefix, kXmpContainerItemLabel));
  const string kLength(Name(kXmpContainerItemPrefix, kXmpContainerItemLength));
  const string kMime(Name(kXmpContainerItemPrefix, kXmpContainerItemMime));
  const string kPadding(Name(kXmpContainerItemPrefix, kXmpContainerItemPadding));    // NOLINT
  const string kSemantic(Name(kXmpContainerItemPrefix, kXmpContainerItemSemantic));  // NOLINT
  const string kDataUri(Name(kXmpContainerItemPrefix, kXmpContainerItemDataUri));    // NOLINT
  // clang-format on
  size_t directory_depth = writer->StartWritingElements({kDirectory, kSeq});
  for (const auto& item : metadata_.items) {
    size_t item_depth = writer->StartWritingElement(kLi);
    writer->WriteAttributeNameAndValue(kXmpRdfRdfParseType,
                                       kXmpRdfParseTypeResource);
    writer->StartWritingElement(kItem);
    MaybeWriteAttributeNameAndValue(kSemantic, item.semantic, writer);
    MaybeWriteAttributeNameAndValue(kMime, item.mime, writer);
    MaybeWriteAttributeNameAndValue(kDataUri, item.data_uri, writer);
    MaybeWriteAttributeNameAndValue(kLabel, item.label, writer);
    MaybeWriteAttributeNameAndValue(kLength, item.length, writer);
    MaybeWriteAttributeNameAndValue(kPadding, item.padding, writer);
    writer->FinishWritingElementsToDepth(item_depth);
  }
  writer->FinishWritingElementsToDepth(directory_depth);
}

}  // namespace image_io
}  // namespace photos_editing_formats
