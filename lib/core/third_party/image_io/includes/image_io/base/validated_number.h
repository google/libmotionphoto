#ifndef IMAGE_IO_BASE_VALIDATED_NUMBER_H_  // NOLINT
#define IMAGE_IO_BASE_VALIDATED_NUMBER_H_  // NOLINT

#include <sstream>
#include <string>

namespace photos_editing_formats {
namespace image_io {

// Further qualifications that can be placed on a numerical value.
enum ValidatedValueType {
  kInvalidValue,
  kValidValue,
  kValidValueGt0,
  kValidValueGe0,
  kValidValueLt0,
  kValidValueLe0,
  kValidValueEq0,
  kValidValueNe0,
};

template <class T>
struct ValidatedNumber {
  ValidatedNumber() : ValidatedNumber(T(), false) {}
  ValidatedNumber(const T& value_, bool is_valid_)
      : value(value_), is_valid(is_valid_) {}
  using value_type = T;
  T value;
  bool is_valid;
};

template <class T>
ValidatedNumber<T> GetValidatedNumber(const std::string& str) {
  std::stringstream ss(str);
  ValidatedNumber<T> result;
  ss >> result.value;
  if (!ss.fail()) {
    std::string extra;
    ss >> extra;
    if (extra.empty()) {
      result.is_valid = true;
    }
  }
  return result;
}

//   number: The number value to validate.
//   type: The required value type to use in the validation.
// Returns whether the value meets the requirements of the type.
template <class T>
bool ValidateValueType(ValidatedNumber<T> number, ValidatedValueType type) {
  switch (type) {
    case kInvalidValue:
      return !number.is_valid;
    case kValidValue:
      return number.is_valid;
    case kValidValueGt0:
      return number.is_valid && number.value > 0;
    case kValidValueGe0:
      return number.is_valid && number.value >= 0;
    case kValidValueLt0:
      return number.is_valid && number.value < 0;
    case kValidValueLe0:
      return number.is_valid && number.value <= 0;
    case kValidValueEq0:
      return number.is_valid && number.value == 0;
    case kValidValueNe0:
      return number.is_valid && number.value != 0;
  }
  return false;
}

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_BASE_VALIDATED_NUMBER_H_  // NOLINT
