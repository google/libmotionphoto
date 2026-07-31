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

#ifndef MOTION_PHOTO_COMMON_H_
#define MOTION_PHOTO_COMMON_H_

namespace photos_editing_formats {
namespace image_io {
class DataSegment;
class DataSource;
class MessageHandler;
class IsoAtom;
class IsoBaseDescriptor;
class IsoDecoder;
class IsoEncoder;
}  // namespace image_io
}  // namespace photos_editing_formats

namespace libmotionphoto {
namespace image_io = ::photos_editing_formats::image_io;
}  // namespace libmotionphoto

#endif // MOTION_PHOTO_COMMON_H_
