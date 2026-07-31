#ifndef IMAGE_IO_XMP_XML_VALUE_H_  // NOLINT
#define IMAGE_IO_XMP_XML_VALUE_H_  // NOLINT

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "image_io/base/data_range.h"
#include "image_io/base/validated_number.h"
#include "image_io/xmp/xmp_errors.h"

namespace photos_editing_formats {
namespace image_io {

/// The value of an XMP's XML element or attribute represents not only the
/// underlying value, but also flags that indicate if the value was assigned
/// from the XMP's XML, and if the assignment was valid.
template <class T>
struct XmpValue {
 public:
  /// @param value The underlying value.
  /// @param was_assigned The value to assign to the was_assigned flag.
  /// @param is_valid The value to assign to the is_valid_flag.
  XmpValue(const T& value, bool was_assigned, bool is_valid)
      : value_(value), was_assigned_(was_assigned), is_valid_(is_valid) {}
  XmpValue() : XmpValue(T(), false, false) {}
  explicit XmpValue(const T& value) : XmpValue(value, true, true) {}
  explicit XmpValue(const ValidatedNumber<T>& vnumber) { SetValue(vnumber); }

  /// The equality and inequality operators.
  bool operator==(const XmpValue<T>& rhs) const {
    return was_assigned_ == rhs.was_assigned_ && is_valid_ == rhs.is_valid_ &&
           value_ == rhs.value_;
  }
  bool operator!=(const XmpValue<T>& rhs) const { return !(*this == rhs); }

  /// @return The underlying value.
  const T& GetValue() const { return value_; }

  /// @return Whether the value is valid.
  bool IsValid() const { return is_valid_; }

  /// @return Whether the value was assigned.
  bool WasAssigned() const { return was_assigned_; }

  /// The assignment operator calls the SetValue() member function that also
  /// sets the was_assigned and is_valid flags to true.
  /// @param value The underlying value
  XmpValue<T>& operator=(const T& value) {
    SetValue(value);
    return *this;
  }

  /// The assignment operator calls the SetValue() member function for
  /// ValidatedNumbers, that sets the was_assigned to true, but uses the
  /// is_valid flag from the ValidatedNumber.
  /// @param value A ValidatedNumber to use for assignment purposes.
  XmpValue<T>& operator=(const ValidatedNumber<T>& vnumber) {
    SetValue(vnumber);
    return *this;
  }

  /// Assigns the underlying value and sets the was_assigned and is_valid flags
  /// to true.
  /// @param value The underlying value
  void SetValue(const T& value) {
    value_ = value;
    is_valid_ = true;
    was_assigned_ = true;
  }

  /// Sets the underlying value to the given value if the underlying value
  /// was not assigned previously.
  /// @param value The underlying value
  void SetValueIfUnassigned(const T& value) {
    if (was_assigned_) return;
    SetValue(value);
  }

  /// Assigns the underlying value and sets the was_assigned flag to true, and
  /// uses the ValidateNumber's is_valid flag.
  /// @param value A ValidatedNumber to use for assignment purposes.
  void SetValue(const ValidatedNumber<T>& vnumber) {
    value_ = vnumber.value;
    is_valid_ = vnumber.is_valid;
    was_assigned_ = true;
  }

  /// @return The value and valid flag as a ValidatedNumber.
  ValidatedNumber<T> GetValidatedNumber() const {
    return ValidatedNumber<T>(value_, is_valid_);
  }

  /// Sets the value to the default for the type, and clears the valid and
  /// assigned flags.
  void Clear() {
    value_ = T();
    is_valid_ = false;
    was_assigned_ = false;
  }

  /// @param is_valid The value to assign to the is_valid flag.
  void SetValid(bool is_valid) { is_valid_ = is_valid; }

  /// @param was_assigned The value to assign to the was_assigned flag.
  void SetAssigned(bool was_assigned) { was_assigned_ = was_assigned; }

 private:
  T value_;
  bool was_assigned_;
  bool is_valid_;
};

/// A template function that can be used to assign a string value to a numerical
/// XmpValue, and return a result with an error message if the string could not
/// be converted to the number type.
/// @param str_value The string value to use for the assignment.
/// @param value_name The name of the value used for the error message.
/// @param value_type The type of value expected.
/// @param context The context used to generate the error message.
/// @param value The XmpValue to receive the converted string value.
/// @return A result that has an error message if the string value could not
/// be coverted, or if the XmpValue already had a value.
template <class T>
DataMatchResult SetXmpValue(const std::string& str_value,
                            const std::string& value_name,
                            ValidatedValueType value_type,
                            const XmlTokenContext& context,
                            XmpValue<T>* value) {
  if (value->WasAssigned()) {
    return GetNameAlreadyAssignedErrorResult(value_name, context);
  }
  auto vnumber = GetValidatedNumber<T>(str_value);
  *value = vnumber;
  if (!value->IsValid()) {
    return GetInvalidNumericalValueErrorResult(value_name, context, value_type);
  }
  value->SetValid(ValidateValueType(vnumber, value_type));
  if (!value->IsValid()) {
    return GetInvalidNumericalValueErrorResult(value_name, context, value_type);
  }
  return context.GetResult();
}

/// A template function that can be used to assign a string value to a numerical
/// XmpValue, and return a result with an error message if the string could not
/// be converted to the number type.
/// @param str_value The string value to use for the assignment.
/// @param value_name The name of the value used for error message.
/// @param context The context used to generate the error message.
/// @param value The XmpValue to receive the converted string value.
/// @return A result that has an error message if the string value could not
/// be coverted, or if the XmpValue already had a value.
template <class T>
DataMatchResult SetXmpValue(const std::string& str_value,
                            const std::string& value_name,
                            const XmlTokenContext& context,
                            XmpValue<T>* value) {
  return SetXmpValue(str_value, value_name, kValidValue, context, value);
}

/// A template function that can be used to assign a string value to a string
/// based XmpValue, and return a result with an error message if the XmpValue
/// already had a value.
/// @param str_value The string value to use for the assignment.
/// @param value_name The name of the value used for the error message.
/// @param context The context used to generate the error message.
/// @param value The XmpValue to receive the string value.
/// @return A result that has an error message if the XmpValue had a value.
inline DataMatchResult SetXmpValue(const std::string& str_value,
                                   const std::string& value_name,
                                   const XmlTokenContext& context,
                                   XmpValue<std::string>* value) {
  if (value->WasAssigned()) {
    return GetNameAlreadyAssignedErrorResult(value_name, context);
  }
  value->SetValue(str_value);
  return context.GetResult();
}

/// A function similar to the SetXmpValue() function above that can be used to
/// assign a vector or ranges to a data member and return a result with an error
/// message if the vector already has a value.
/// @param ranges The vector of ranges to use for the assignment.
/// @param value_name The name of the value used for the error message.
/// @param context The context used to generate the error message.
/// @param ranges_to_set The vector of ranges to receive the ranges.
/// @return A result that has an error message if ranges_to_set is not empty.
inline DataMatchResult SetRanges(const std::vector<DataRange>& ranges,
                                 const std::string& value_name,
                                 const XmlTokenContext& context,
                                 std::vector<DataRange>* ranges_to_set) {
  if (!ranges_to_set->empty()) {
    return GetNameAlreadyAssignedErrorResult(value_name, context);
  }
  *ranges_to_set = ranges;
  return context.GetResult();
}

/// @param v1 The first value to compare
/// @param v2 The second value to compare
/// @param positive_epsilon The delta value
/// @return Whether the absolute value of the difference between the two
/// values is less than the epsilon value.
template <class T>
bool Near(const XmpValue<T>& v1, const XmpValue<T>& v2, T positive_epsilon) {
  return v1.IsValid() == v2.IsValid() && v1.WasAssigned() == v2.WasAssigned() &&
         std::abs(v1.GetValue() - v2.GetValue()) < positive_epsilon;
}

/// @param s1 The first string to compare
/// @param s2 The second string to compare
/// @return Whether the strings are equal, ignoring case.
inline bool EqualsIgnoreCase(const std::string& s1, const std::string& s2) {
  return s1.length() == s2.length() &&
         std::equal(s1.begin(), s1.end(), s2.begin(), [](char c1, char c2) {
           return std::tolower(c1) == std::tolower(c2);
         });
}

/// @param v The xmp string value to compare
/// @param s The string to compare
/// @return Whether the xmp string value and string are equal, ignoring case.
inline bool EqualsIgnoreCase(const XmpValue<std::string>& v,
                             const std::string& s) {
  return v.IsValid() && v.WasAssigned() && EqualsIgnoreCase(v.GetValue(), s);
}

/// @param v1 The first value to compare
/// @param v2 The second value to compare
/// @return Whether the string values are equal, ignoring case.
inline bool EqualsIgnoreCase(const XmpValue<std::string>& v1,
                             const XmpValue<std::string>& v2) {
  return v1.IsValid() == v2.IsValid() && v1.WasAssigned() == v2.WasAssigned() &&
         EqualsIgnoreCase(v1.GetValue(), v2.GetValue());
}

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XML_VALUE_H_  // NOLINT
