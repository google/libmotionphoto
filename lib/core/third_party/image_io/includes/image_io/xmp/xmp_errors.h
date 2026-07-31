#ifndef IMAGE_IO_XMP_XMP_ERRORS_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_ERRORS_H_  // NOLINT

#include <string>
#include <vector>

#include "image_io/base/validated_number.h"
#include "image_io/xml/xml_token_context.h"

namespace photos_editing_formats {
namespace image_io {

// clang-format off

/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetCdataNotSupportedErrorResult(
    const XmlTokenContext& context);

/// @param name The name which is unrecognized.
/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetUnrecognizedNameErrorResult(
    const std::string& name,
    const XmlTokenContext& context);

/// @param name The name which is deprecated.
/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetDeprecatedNameErrorResult(
    const std::string& name,
    const XmlTokenContext& context);

/// @param name The name which already has a value.
/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetNameAlreadyAssignedErrorResult(
    const std::string& name,
    const XmlTokenContext& context);

/// @param name The name which was given a non-valid value.
/// @param valid_values A vector of valid values (may be empty).
/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetInvalidValueErrorResult(
    const std::string& name,
    const std::vector<std::string>& valid_values,
    const XmlTokenContext& context);

/// @param name The name which was given a non-numerical value.
/// @param context The context used to create an error message.
/// @param expected_value_type The expected type of number value.
/// @return A result containing an error message.
DataMatchResult GetInvalidNumericalValueErrorResult(
    const std::string& name,
    const XmlTokenContext& context,
    ValidatedValueType expected_value_type = kValidValue
);

/// @param name The name which is not in the path.
/// @param element_name_stack The element names in the path.
/// @param element_name The final element name in the path.
/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetNamePathErrorResult(
    const std::string& name,
    const std::vector<std::string>& element_name_stack,
    const std::string& element_name,
    const XmlTokenContext& context);

/// @param name The name which has already appeared in the XMP's XML.
/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetDuplicateElementErrorResult(
    const std::string& name,
    const XmlTokenContext& context);

/// @param name The name which cannot be used as an attribute.
/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetCantBeAnAttributeErrorResult(
    const std::string& name,
    const XmlTokenContext& context);

/// @param name The name the value of which is an invalid list.
/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetInvalidListErrorResult(const std::string& name,
    const XmlTokenContext& context);

/// @param name The name the value of which is an invalid list.
/// @param max_size The max size of the list.
/// @param context The context used to create an error message.
/// @return A result containing an error message.
DataMatchResult GetTooManyListLementsErrorResult(
    const std::string& name,
    size_t max_size,
    const XmlTokenContext& context);

// clang-format on

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_ERRORS_H_  // NOLINT
