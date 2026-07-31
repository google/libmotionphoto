#include "image_io/xmp/xmp_gain_map_metadata_writer.h"

#include <array>
#include <string>

#include "image_io/xmp/xmp_gain_map_metadata.h"
#include "image_io/xmp/xmp_helpers.h"
#include "image_io/xmp/xmp_rdf_constants.h"

namespace photos_editing_formats {
namespace image_io {

namespace {

std::string Name(const std::string& prefix, const std::string& suffix) {
  return JoinPrefixAndName(prefix, suffix);
}

}  // namespace

XmpGainMapMetadataWriter::XmpGainMapMetadataWriter(
    const XmpGainMapMetadata& metadata)
    : metadata_(metadata) {}

void XmpGainMapMetadataWriter::WriteNamespaces(XmlWriter* writer) {
  writer->WriteXmlns(kXmpGainMapPrefix, kXmpGainMapUri);
}

void XmpGainMapMetadataWriter::WriteAttributeNamesAndValues(XmlWriter* writer) {
  const std::string kVersion = Name(kXmpGainMapPrefix, kXmpGainMapVersion);
  if (!MaybeWriteAttributeNameAndValue(kVersion, metadata_.version, writer)) {
    writer->WriteAttributeNameAndValue(kVersion, "1");
  }
  MaybeWriteAttributeNameAndValue(
      Name(kXmpGainMapPrefix, kXmpGainMapBaseRenditionIsHDR),
      metadata_.base_rendition_is_hdr, writer);
  MaybeWriteAttributeNameAndValue(
      Name(kXmpGainMapPrefix, kXmpGainMapHDRCapacityMin),
      metadata_.hdr_capacity_min, writer);
  MaybeWriteAttributeNameAndValue(
      Name(kXmpGainMapPrefix, kXmpGainMapHDRCapacityMax),
      metadata_.hdr_capacity_max, writer);
  WriteFloat3Values(kWriteAttributesPhase, writer);
}

void XmpGainMapMetadataWriter::WriteElements(XmlWriter* writer) {
  WriteFloat3Values(kWriteElementsPhase, writer);
}

void XmpGainMapMetadataWriter::WriteFloat3Values(WritePhase phase,
                                                 XmlWriter* writer) {
  WriteFloat3Value(phase, Name(kXmpGainMapPrefix, kXmpGainMapGainMapMin),
                   metadata_.gain_map_min, writer);
  WriteFloat3Value(phase, Name(kXmpGainMapPrefix, kXmpGainMapGainMapMax),
                   metadata_.gain_map_max, writer);
  WriteFloat3Value(phase, Name(kXmpGainMapPrefix, kXmpGainMapGamma),
                   metadata_.gamma, writer);
  WriteFloat3Value(phase, Name(kXmpGainMapPrefix, kXmpGainMapOffsetSDR),
                   metadata_.offset_sdr, writer);
  WriteFloat3Value(phase, Name(kXmpGainMapPrefix, kXmpGainMapOffsetHDR),
                   metadata_.offset_hdr, writer);
}

void XmpGainMapMetadataWriter::WriteFloat3Value(
    WritePhase phase, const std::string& name,
    const XmpValue<XmpGainMapMetadata::Float3>& value, XmlWriter* writer) {
  if (!value.IsValid()) {
    return;
  }
  const auto& floatValues = value.GetValue();
  bool homogenousValues =
      floatValues[0] == floatValues[1] && floatValues[1] == floatValues[2];
  if (phase == kWriteAttributesPhase && homogenousValues) {
    writer->WriteAttributeNameAndValue(name, floatValues[0]);
  } else if (phase == kWriteElementsPhase && !homogenousValues) {
    const std::string kSeq(Name(kXmpRdfPrefix, kXmpRdfSeq));
    const std::string kLi(Name(kXmpRdfPrefix, kXmpRdfLi));
    size_t depth = writer->StartWritingElements({name, kSeq});
    for (const auto& floatValue : floatValues) {
      writer->WriteElementAndContent(kLi, floatValue);
    }
    writer->FinishWritingElementsToDepth(depth);
  }
}

}  // namespace image_io
}  // namespace photos_editing_formats
