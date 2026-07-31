#ifndef IMAGE_IO_XMP_XMP_CONTAINER_METADATA_READER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_CONTAINER_METADATA_READER_H_  // NOLINT

#include "image_io/xmp/xmp_container_metadata.h"
#include "image_io/xmp/xmp_content_handler.h"

namespace photos_editing_formats {
namespace image_io {

/// An XMP content handler for the Container and Container:Item metadata.
/// Use an instance of this class with an XmpReader.
class XmpContainerMetadataReader : public XmpContentHandler {
 public:
  // clang-format off
  XmpContainerMetadataReader();
  void SetUriPrefix(const std::string& uri, const std::string& prefix) override;
  DataMatchResult ProcessElementName(
      const StringVector& element_name_stack, const std::string& element_name,
      const XmlTokenContext& context)
      override;
  DataMatchResult ProcessElementContent(
      const StringVector& element_name_stack, const std::string& element_name,
      const XmpContent& content, const XmlTokenContext& context)
      override;
  DataMatchResult ProcessAttributeName(
      const StringVector& element_name_stack, const std::string& element_name,
      const std::string& attribute_name, const XmlTokenContext& context)
      override;
  // clang-format on

  /// @return The container metadata read from the XMP data.
  const XmpContainerMetadata& GetContainerMetadata() const {
    return container_metadata_;
  }

 private:
  /// These functions check the element name stack context where an element
  /// appears in the XMP's XML and return a result with an error message if
  /// the context is not correct. Some of the functions also do some setup for
  /// the subsequent processing of elements and attributes.
  DataMatchResult CheckVersionContext(const StringVector& element_name_stack,
                                      const std::string& element_name,
                                      const XmlTokenContext& context);
  DataMatchResult CheckDirectoryContext(const StringVector& element_name_stack,
                                        const std::string& element_name,
                                        const XmlTokenContext& context);
  DataMatchResult CheckItemContext(const StringVector& element_name_stack,
                                   const std::string& element_name,
                                   const XmlTokenContext& context);
  DataMatchResult CheckItemChildContext(const StringVector& element_name_stack,
                                        const std::string& element_name,
                                        const XmlTokenContext& context);

  /// @param element_name_stack The current element name stack.
  /// @param element_name The current element name.
  /// @return Whether the stack and name represent a valid rdf:Description
  ///     inside a container's rdf:li element. If so, this indicates a canonical
  ///     XMP type representation of the list element structure.
  bool IsRdfLiDescriptionContext(const StringVector& element_name_stack,
                                 const std::string& element_name);

  /// The container metadata that is read from the XMP's XML.
  XmpContainerMetadata container_metadata_;

  /// Whether the container directory element has been processed. Only one
  /// directory element can appear in the XMP's XML read by this handler.
  bool directory_element_processed_;

  /// The size of the element stack when a rdf:Description is encountered inside
  /// an rdf:li element. This indicates a canonical XMP type representation of
  /// the list element structure.
  size_t rdf_li_description_context_stack_size_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_CONTAINER_METADATA_READER_H_  // NOLINT
