#ifndef IMAGE_IO_XMP_XMP_GAIN_MAP_METADATA_READER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_GAIN_MAP_METADATA_READER_H_  // NOLINT

#include <string>
#include <unordered_map>
#include <vector>

#include "image_io/base/data_match_result.h"
#include "image_io/base/validated_number.h"
#include "image_io/xml/xml_token_context.h"
#include "image_io/xmp/xmp_content_handler.h"
#include "image_io/xmp/xmp_element_handler.h"
#include "image_io/xmp/xmp_gain_map_metadata.h"

namespace photos_editing_formats {
namespace image_io {

/// An XMP content handler for the HDR gain map metadata.
/// Use an instance of this class with an XmpReader.
class XmpGainMapMetadataReader : public XmpContentHandler {
 public:
  XmpGainMapMetadataReader();
  void SetUriPrefix(const std::string& uri, const std::string& prefix) override;
  DataMatchResult ProcessElementName(const StringVector& element_name_stack,
                                     const std::string& element_name,
                                     const XmlTokenContext& context) override;
  DataMatchResult ProcessElementContent(
      const StringVector& element_name_stack, const std::string& element_name,
      const XmpContent& content, const XmlTokenContext& context) override;

  /// @return The gain map metadata read from the XMP data.
  const XmpGainMapMetadata& GetGainMapMetadata() const {
    return gain_map_metadata_;
  }

 private:
  using XmpFloatValue = XmpValue<float>;
  using XmpFloat3Value = XmpValue<XmpGainMapMetadata::Float3>;
  void AddSupportedFloatName(const std::string& name, XmpFloatValue* value,
                             ValidatedValueType expected_type);
  void AddSupportedFloat3Name(const std::string& name, XmpFloat3Value* value,
                              ValidatedValueType expected_type);
  void AddSupportedNameAndValueType(const std::string& name,
                                    ValidatedValueType type);
  DataMatchResult ProcessRdfSeqName(const StringVector& element_name_stack,
                                    const std::string& element_name,
                                    const XmlTokenContext& context);
  DataMatchResult ProcessRdfSeqLiContent(const StringVector& element_name_stack,
                                         const std::string& element_name,
                                         const XmpContent& content,
                                         const XmlTokenContext& context);
  DataMatchResult ProcessRdfSeqContent(const StringVector& element_name_stack,
                                       const std::string& element_name,
                                       const XmpContent& content,
                                       const XmlTokenContext& context);
  std::string GetListElementName(const StringVector& element_name_stack,
                                 size_t index_from_back) const;
  ValidatedValueType GetValueType(const std::string& name) const;

  /// The metadata element name being assigned via the RDF:Seq/Li syntax.
  std::string list_element_name_;

  /// A vector of values used when RDF:Seq/Li syntax is used to specify a
  /// Float3 value of one of the properties.
  std::vector<XmpValue<float>> list_float_values_;

  /// A map of element names to float values.
  std::unordered_map<std::string, XmpFloatValue*> float_value_map_;

  /// A map of element names to Float3 values.
  std::unordered_map<std::string, XmpFloat3Value*> float3_value_map_;

  /// The value types to use when checking numerical values.
  std::unordered_map<std::string, ValidatedValueType>
      supported_name_value_type_map_;

  /// The gain map metadata that is read from the XMP's XML.
  XmpGainMapMetadata gain_map_metadata_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_GAIN_MAP_METADATA_READER_H_  // NOLINT
