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

#ifndef MOTION_PHOTO_MPVD_BOX_H  // NOLINT
#include <cstdint>
#define MOTION_PHOTO_MPVD_BOX_H  // NOLINT

#include "image_io/iso/iso_atom.h"

#include "motion_photo/common.h"

namespace libmotionphoto {
namespace motion_photo {

// A class that decodes/encodes the mpvd box for HEIC motion photos.
class MpvdBox : public image_io::IsoAtom {
 public:
  // The 4 character box type value.
  static constexpr char kType[] = "mpvd";

  //   atom: The atom to convert to an MpvdBox
  // Returns a MpvdBox which may be invalid if the atom does not represent a
  // valid MpvdBox.
  static MpvdBox Convert(const image_io::IsoAtom& atom);

  // Use the default constructor for use with the Decode function.
  MpvdBox();

  // Use this constructor if you intend to use the Encode function.
  //   video_length: the length of the video to encode in the box.
  explicit MpvdBox(uint64_t video_length);

  // Returns the index in the buffer where the box started.
  uint64_t GetStartIndex() const { return start_index_; }

  //   start_index: The index in the buffer where the box started.
  void SetStartIndex(uint64_t start_index) {
    start_index_ = start_index;
  }

  // Returns the length of the video inside the mpvd box.
  uint64_t GetVideoLength() const { return GetPayloadLength(); }

  bool IsValid() const override;

  // The equality operators.
  bool operator!=(const MpvdBox& rhs) const { return !((*this) == rhs); }
  bool operator==(const MpvdBox& rhs) const {
    return image_io::IsoAtom::operator==(rhs) &&
           start_index_ == rhs.start_index_;
  }

 protected:
  int DecodeWorker(image_io::DataSource* data_source,
                   uint64_t start) override;

 private:
  uint64_t start_index_;
};

// This struct can be used with the `MpvdBox`::Scan functions to find the
// MpvdBox in data source of vector/buffer of bytes. Usage is like this:
// MpvdBoxScanReceiver receiver;
// int status = MpvdBox::Scan(data_source, receiver);
// if (status == MpvdBox::kDecodeOk && receiver.mpvd_box.IsValid()) {
//   // do something with the receiver.mpvd_box
// }
class MpvdBoxScanReceiver {
 public:
  //   mpvd_box: The box into which the scan results will be placed.
  explicit MpvdBoxScanReceiver(MpvdBox& mpvd_box) : mpvd_box_(mpvd_box) {}

  // A functor operator so that an instance of this class can be passed
  // directly to the `MpvdBox`::Scan functions as the ScanReceiver parameter.
  uint64_t operator()(uint64_t start,
                      const image_io::IsoAtom& atom) {
    if (atom.GetType() == MpvdBox::kType) {
      mpvd_box_ = MpvdBox::Convert(atom);
      mpvd_box_.SetStartIndex(start);
      return start;
    }
    return start + atom.GetLength();
  }

 private:
  MpvdBox& mpvd_box_;
};

}  // namespace motion_photo
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_MPVD_BOX_H // NOLINT
