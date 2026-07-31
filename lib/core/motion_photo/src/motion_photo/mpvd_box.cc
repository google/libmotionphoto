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

#include "motion_photo/mpvd_box.h"

#include <iostream>
#include <limits>

#include "image_io/base/types.h"

namespace libmotionphoto {
namespace motion_photo {

using image_io::DataSource;

MpvdBox MpvdBox::Convert(const IsoAtom& atom) {
  if (!atom.IsValid() || atom.GetType() != kType) {
    return MpvdBox();
  }
  MpvdBox box(atom.GetPayloadLength());
  if (atom.GetHeaderLength() == kStandardHeaderLength) {
    box.ReduceHeaderLengthIfPossible();
  }
  return box;
}

MpvdBox::MpvdBox() : IsoAtom(0, 0, kType), start_index_(0) {}

MpvdBox::MpvdBox(uint64_t video_length)
    : IsoAtom(IsoAtom::kExtendedHeaderLength, video_length, kType),
      start_index_(0) {}

bool MpvdBox::IsValid() const {
  bool is_valid = IsoAtom::IsValid();
  if (is_valid && GetType() != kType) {
    is_valid = false;
  }
  return is_valid;
}

int MpvdBox::DecodeWorker(DataSource* data_source, uint64_t start) {
  int status = IsoAtom::DecodeWorker(data_source, start);
  if (status == kDecodeOk && GetType() != kType) {
    status = kDecodeErrorInvalidAtomType;
  }
  return status;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
