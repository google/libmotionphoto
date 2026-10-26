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

#ifndef MOTION_PHOTO_HEIF_HEIF_ISO_META_EDITOR_H_
#define MOTION_PHOTO_HEIF_HEIF_ISO_META_EDITOR_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace libmotionphoto {
namespace motion_photo {

// Lossless editor for the ISO-BMFF structure of HEIF (HEIC/AVIF) files.
//
// Unlike a decode/re-encode round trip, the editor never touches coded image
// data: it only rewrites the 'meta' box item tables ('iinf', 'iloc', 'iref')
// and moves byte ranges, so every image item, tile grid, gain map, Exif and ICC
// property is preserved bit-exactly. It has no codec dependency and behaves the
// same on Android and non-Android platforms.
class HeifIsoMetaEditor {
 public:
  // Writes into `output` a copy of the HEIF file `data` in which:
  //   - the XMP item describing the primary image (an 'infe' of type 'mime'
  //     with content type "application/rdf+xml" and a 'cdsc' reference to the
  //     'pitm' item) holds exactly `xmp` (padded with trailing XML whitespace
  //     when it is updated in place). The item is created if it does not exist.
  //   - top-level motion photo trailer boxes ('mpvd', 'sefd') are removed, so
  //     the result is a plain still image that callers may append a new 'mpvd'
  //     box to.
  // Returns false (with a reason in `error` when non-null) if the input is not
  // a well-formed HEIF file or uses a layout the editor cannot rewrite safely;
  // `output` is left unspecified in that case.
  static bool SetPrimaryXmp(const uint8_t* data, size_t size,
                            std::string_view xmp, std::vector<uint8_t>* output,
                            std::string* error = nullptr);
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_HEIF_HEIF_ISO_META_EDITOR_H_
