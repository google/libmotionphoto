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

#ifndef MOTION_PHOTO_HEIC_LIBHEIF_DELETER_H_  // NOLINT
#define MOTION_PHOTO_HEIC_LIBHEIF_DELETER_H_  // NOLINT

#if defined(MOTION_PHOTO_HEIC_SUPPORT)
#include <libheif/heif.h>

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

struct LibHeifDeleter {
  void operator()(heif_context* heif_context) {
    heif_context_free(heif_context);
  }
  void operator()(heif_image_handle* heif_image_handle) {
    heif_image_handle_release(heif_image_handle);
  }
  void operator()(heif_image* heif_img) { heif_image_release(heif_img); }
  void operator()(heif_encoder* heif_encoder) {
    heif_encoder_release(heif_encoder);
  }
};

}  // namespace motion_photo
}  // namespace libmotionphoto
#endif  // defined(MOTION_PHOTO_HEIC_SUPPORT)

#endif  // MOTION_PHOTO_HEIC_LIBHEIF_DELETER_H_  // NOLINT
