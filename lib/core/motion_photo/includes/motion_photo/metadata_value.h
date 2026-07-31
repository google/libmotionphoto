// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MOTION_PHOTO_METADATA_VALUE_H_
#define MOTION_PHOTO_METADATA_VALUE_H_

#include <cctype>
#include <string>

namespace libmotionphoto {
namespace motion_photo {

/// A container for metadata values that tracks whether a field was assigned
/// and whether its value is valid.
template <class T>
struct MetadataValue {
 public:
  MetadataValue(const T& value, bool was_assigned, bool is_valid)
      : value_(value), was_assigned_(was_assigned), is_valid_(is_valid) {}
  MetadataValue() : MetadataValue(T(), false, false) {}
  explicit MetadataValue(const T& value) : MetadataValue(value, true, true) {}

  bool operator==(const MetadataValue<T>& rhs) const {
    return was_assigned_ == rhs.was_assigned_ && is_valid_ == rhs.is_valid_ &&
           value_ == rhs.value_;
  }
  bool operator!=(const MetadataValue<T>& rhs) const { return !(*this == rhs); }

  const T& GetValue() const { return value_; }
  bool IsValid() const { return is_valid_; }
  bool WasAssigned() const { return was_assigned_; }

  MetadataValue<T>& operator=(const T& value) {
    SetValue(value);
    return *this;
  }

  void SetValue(const T& value) {
    value_ = value;
    was_assigned_ = true;
    is_valid_ = true;
  }

  void Clear() {
    value_ = T();
    was_assigned_ = false;
    is_valid_ = false;
  }

 private:
  T value_;
  bool was_assigned_;
  bool is_valid_;
};

inline bool EqualsIgnoreCase(const std::string& s1, const std::string& s2) {
  if (s1.length() != s2.length()) {
    return false;
  }
  for (size_t i = 0; i < s1.length(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s1[i])) !=
        std::tolower(static_cast<unsigned char>(s2[i]))) {
      return false;
    }
  }
  return true;
}

inline bool EqualsIgnoreCase(const MetadataValue<std::string>& v,
                             const std::string& s) {
  return v.IsValid() && EqualsIgnoreCase(v.GetValue(), s);
}

inline bool EqualsIgnoreCase(const std::string& s,
                             const MetadataValue<std::string>& v) {
  return EqualsIgnoreCase(v, s);
}

inline bool EqualsIgnoreCase(const MetadataValue<std::string>& v1,
                             const MetadataValue<std::string>& v2) {
  return v1.IsValid() && v2.IsValid() &&
         EqualsIgnoreCase(v1.GetValue(), v2.GetValue());
}

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_METADATA_VALUE_H_
