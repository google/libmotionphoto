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

#include "motion_photo_mp4/motion_photo_mp4_file_builder.h"

#include <mp4v2/file.h>
#include <mp4v2/general.h>

#include <cerrno>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "image_io/utils/file_utils.h"
#include "motion_photo/video_track_data.h"

// The third_party/mp4v2/src/src.h header is needed to write the mett atom to
// the MP4 file. This using statement in this nested namespace is neeeded to
// complie the header without errors.
namespace mp4v2 {
namespace impl {
using std::string;
}
}  // namespace mp4v2

#include "src/src.h"  // NOLINT

namespace libmotionphoto {
namespace motion_photo {

using image_io::GetFileSize;
using image_io::Message;
using image_io::MessageHandler;
using mp4v2::impl::MP4Atom;
using mp4v2::impl::MP4BytesProperty;
using mp4v2::impl::MP4File;
using mp4v2::impl::MP4Integer16Property;
using mp4v2::impl::MP4Property;
using mp4v2::impl::MP4StringProperty;
using std::ifstream;
using std::ofstream;
using std::string;
using std::stringstream;
using std::vector;

namespace {

// Constants for creating the meta track with the mett atom.
const char kMett[] = "mett";
const char kMp4s[] = "mp4s";
const char kMp4sPath[] = "mdia.minf.stbl.stsd.mp4s";
const char kStsd[] = "stsd";

// The current third_party/mp4v2 library does not have a mett atom. This is an
// implementation of said atom for use with the motion photo builder function.
class MP4MettAtom : public MP4Atom {
 public:
  explicit MP4MettAtom(MP4File& file) : MP4Atom(file, kMett) {  // NOLINT
    AddReserved(*this, "reserved1", 6);                         // 0
    AddProperty(new MP4Integer16Property(*this, "dataReferenceIndex"));  // 1
    AddProperty(new MP4StringProperty(*this, "contentEncoding"));        // 2
    AddProperty(new MP4StringProperty(*this, "mimeFormat"));             // 3
  }
  void Generate() override {
    MP4Atom::Generate();
    SetDataReferenceIndex(1);
  }
  void SetDataReferenceIndex(uint16_t index) {
    ((MP4Integer16Property*)m_pProperties[1])->SetValue(index);  // NOLINT
  }
  void SetMimeFormat(const char* mime_format) {
    const string kAppPrefix = "application";
    ((MP4StringProperty*)m_pProperties[3])->SetValue(mime_format);  // NOLINT
    // This next bit of code is to work around the bad implementation of the
    // mett atom in the android framework prior to the Q release. The bad code
    // assumed incorrectly that the mimeFormat value was the only property in
    // the atom - no reserved bytes, no data reference index and no content
    // encoding. So the work around is to write the mime type twice. The first
    // occurrence is spread across the reserved bytes and the data reference
    // and content encoding values (and terminated by the content encoding
    // value's final null character), and then again in the mime format itself.
    // For more info and context, see Android framework Q metadata atom
    // compatibility requirements.
    if (strncmp(mime_format, kAppPrefix.c_str(), kAppPrefix.length()) == 0) {
      MP4BytesProperty* bytes = (MP4BytesProperty*)m_pProperties[0];  // NOLINT
      bytes->SetReadOnly(false);
      bytes->SetValue(reinterpret_cast<const uint8_t*>(kAppPrefix.c_str()), 6);
      SetDataReferenceIndex(0x6174);  // 'at'
      ((MP4StringProperty*)m_pProperties[2])
          ->SetValue(mime_format + 8);  // NOLINT
    }
  }

 private:
  MP4MettAtom();
  MP4MettAtom(const MP4MettAtom& src);
  MP4MettAtom& operator=(const MP4MettAtom& src);
};

//   file_handle: The file to close if needed.
void CloseMp4FileIfNeeded(MP4FileHandle* file_handle) {
  if (*file_handle != MP4_INVALID_FILE_HANDLE) {
    MP4Close(*file_handle);
    *file_handle = MP4_INVALID_FILE_HANDLE;
  }
}

//   file: The file to get the track summary of.
// Returns a string containing the track summary of the file.
string GetMp4TrackSummary(MP4FileHandle file) {
  stringstream ss;
  char* info = MP4Info(file, MP4_INVALID_TRACK_ID);
  if (info != nullptr) {
    ss << info;
    free(info);
  }
  return ss.str();
}

//   input_file_name: The file to copy.
//   output_file_name: The place to copy the input file to.
// Returns whether the file was copied successfully.
bool CopyFile(std::string_view input_file_name,
              std::string_view output_file_name) {
  ifstream source(std::string(input_file_name), std::ios::binary);
  if (!source.is_open()) {
    return false;
  }
  ofstream dest(std::string(output_file_name), std::ios::binary);
  if (!dest.is_open()) {
    return false;
  }
  dest << source.rdbuf();
  source.close();
  dest.close();
  return true;
}

//   handle: The file in which to replace the meta track's mp4s atom
//   track_id: The id of the meta track.
// Returns whether the mp4s atom was replaced with a mett atom successfully.
bool ReplaceMp4sAtomWithMettAtom(MP4FileHandle handle, MP4TrackId track_id) {
  auto* file = reinterpret_cast<mp4v2::impl::MP4File*>(handle);
  auto* mp4s = file->FindTrackAtom(track_id, kMp4sPath);
  MP4Atom* stsd = mp4s ? mp4s->GetParentAtom() : nullptr;
  MP4Property* property = mp4s ? mp4s->GetProperty(1) : nullptr;
  if (mp4s && stsd && strcmp(kMp4s, mp4s->GetType()) == 0 &&
      strcmp(kStsd, stsd->GetType()) == 0 && property &&
      property->GetType() == mp4v2::impl::Integer16Property) {
    MP4MettAtom* mett = new MP4MettAtom(*file);
    uint16_t index = static_cast<MP4Integer16Property*>(property)->GetValue();
    mett->SetDataReferenceIndex(index);
    mett->SetMimeFormat(kMotionPhotoMetadataMime);
    stsd->DeleteChildAtom(mp4s);
    stsd->AddChildAtom(mett);
    return true;
  }
  return false;
}

}  // namespace

MotionPhotoMp4FileBuilder::~MotionPhotoMp4FileBuilder() {
  CloseMp4FileIfNeeded(&file_handle_);
}

MotionPhotoMp4FileBuilder::MotionPhotoMp4FileBuilder(
    const string& working_file_name, MessageHandler* message_handler)
    : message_handler_(message_handler),
      working_file_name_(working_file_name),
      file_handle_(MP4_INVALID_FILE_HANDLE),
      open_file_failed_(false) {}

bool MotionPhotoMp4FileBuilder::AddPrimaryMp4FileTracks(
    const string& primary_file_name, string* track_summary) {
  if (!CopyFile(primary_file_name, working_file_name_)) {
    return false;
  }

  file_handle_ = MP4Modify(working_file_name_.c_str());
  if (file_handle_ == MP4_INVALID_FILE_HANDLE) {
    message_handler_->ReportMessage(Message::kStdLibError, working_file_name_);
    return false;
  }

  uint32_t track_count = MP4GetNumberOfTracks(file_handle_, nullptr, 0);
  for (int index = (int)track_count - 1; index >= 0; --index) {
    MP4TrackId track_id = MP4FindTrackId(file_handle_, index, nullptr, 0);
    const char* track_type = MP4GetTrackType(file_handle_, track_id);
    if (track_type && strcmp(track_type, kVideoMetadataTrackType) == 0) {
      MP4DeleteTrack(file_handle_, track_id);
    }
  }

  *track_summary = GetMp4TrackSummary(file_handle_);
  return true;
}

bool MotionPhotoMp4FileBuilder::AddMomentsMp4FileTrack(
    const string& moments_file_name, string* track_summary) {
  MP4FileHandle handle = MP4Read(moments_file_name.c_str());
  if (handle == MP4_INVALID_FILE_HANDLE) {
    errno = EIO;
    message_handler_->ReportMessage(Message::kStdLibError, moments_file_name);
    return false;
  }
  *track_summary = GetMp4TrackSummary(handle);
  if (!OpenFileIfNeeded()) {
    return false;
  }
  stringstream trackss;
  uint32_t track_count = MP4GetNumberOfTracks(handle, NULL, 0);  // NOLINT
  for (uint32_t index = 0; index < track_count; ++index) {
    MP4TrackId src_id = MP4FindTrackId(handle, index, NULL, 0);  // NOLINT
    MP4TrackId dst_id =
        MP4CopyTrack(handle, src_id, file_handle_, false, MP4_INVALID_TRACK_ID);
    if (dst_id == MP4_INVALID_TRACK_ID) {
      trackss << std::endl << "- track id " << src_id << " was not copied";
    }
  }
  if (!trackss.str().empty()) {
    stringstream ss;
    ss << "Copying tracks from: " << moments_file_name << trackss.str();
    errno = EIO;
    message_handler_->ReportMessage(Message::kStdLibError, ss.str());
    CloseMp4FileIfNeeded(&handle);
    return false;
  }
  return true;
}

bool MotionPhotoMp4FileBuilder::AddMomentsMetadataTrack(
    const vector<uint8_t>& metadata) {
  if (!OpenFileIfNeeded()) {
    return false;
  }
  stringstream errss;
  MP4TrackId track_id =
      MP4AddTrack(file_handle_, kVideoMetadataTrackType, MP4_MSECS_TIME_SCALE);
  if (track_id == MP4_INVALID_TRACK_ID) {
    errss << "Can't create metadata track: " << working_file_name_;
  } else {
    if (!ReplaceMp4sAtomWithMettAtom(file_handle_, track_id)) {
      errss << "Can't create metadata descriptor: " << working_file_name_;
    } else {
      if (!MP4WriteSample(file_handle_, track_id, metadata.data(),
                          metadata.size(), MP4_INVALID_DURATION, 0, true)) {
        errss << "Can't write metadata sample: " << working_file_name_;
      }
    }
  }
  if (!errss.str().empty()) {
    errno = EIO;
    message_handler_->ReportMessage(Message::kStdLibError, errss.str());
    return false;
  }
  return true;
}

bool MotionPhotoMp4FileBuilder::OptimizeMp4File(string* track_summary,
                                                size_t* file_size) {
  *track_summary = GetMp4TrackSummary(file_handle_);
  CloseMp4FileIfNeeded(&file_handle_);
#ifndef DISABLE_MP4_OPTIMIZE
  if (!MP4Optimize(working_file_name_.c_str(), nullptr)) {
    errno = EIO;
    stringstream ss;
    ss << "Can't optimize file: " << working_file_name_;
    message_handler_->ReportMessage(Message::kStdLibError, ss.str());
    return false;
  }
#endif
  GetFileSize(working_file_name_, file_size);
  return true;
}

bool MotionPhotoMp4FileBuilder::OpenFileIfNeeded() {
  if (open_file_failed_) {
    return false;
  }
  if (file_handle_ != MP4_INVALID_FILE_HANDLE) {
    return true;
  }
  file_handle_ = MP4Modify(working_file_name_.c_str());
  if (file_handle_ != MP4_INVALID_FILE_HANDLE) {
    return true;
  }
  errno = EIO;
  message_handler_->ReportMessage(Message::kStdLibError, working_file_name_);
  open_file_failed_ = true;
  return false;
}

string MotionPhotoMp4FileBuilder::GetFileTrackSummary(
    const string& file_name, MessageHandler* message_handler) {
  MP4FileHandle handle = MP4Read(file_name.c_str());
  if (handle == MP4_INVALID_FILE_HANDLE) {
    errno = EIO;
    message_handler->ReportMessage(Message::kStdLibError, file_name);
    return "";
  }
  string summary = GetMp4TrackSummary(handle);
  MP4Close(handle);
  return summary;
}

bool MotionPhotoMp4FileBuilder::IsOptimizationSupported(
    const string& file_name, MessageHandler* message_handler) {
  MP4FileHandle src_handle = MP4Read(file_name.c_str());
  if (src_handle == MP4_INVALID_FILE_HANDLE) {
    errno = EIO;
    message_handler->ReportMessage(Message::kStdLibError, file_name);
    return false;
  }
  bool supported = true;
  uint32_t track_count = MP4GetNumberOfTracks(src_handle, nullptr, 0);
  for (uint32_t index = 0; index < track_count; ++index) {
    MP4TrackId src_id = MP4FindTrackId(src_handle, index, nullptr, 0);
    const char* track_type = MP4GetTrackType(src_handle, src_id);
    if (strcmp(track_type, MP4_VIDEO_TRACK_TYPE) == 0) {
      const char* media_data_name = MP4GetTrackMediaDataName(src_handle, src_id);
      if (media_data_name && strcmp(media_data_name, "hvc1") == 0) {
        supported = false;
        break;
      }
    }
  }
  MP4Close(src_handle);
  return supported;
}

bool MotionPhotoMp4FileBuilder::StripMetadataTrack(
    const string& file_name, MessageHandler* message_handler) {
  MP4FileHandle read_handle = MP4Read(file_name.c_str());
  if (read_handle == MP4_INVALID_FILE_HANDLE) {
    message_handler->ReportMessage(Message::kStdLibError, "MP4Read failed: " + file_name);
    return false;
  }

  bool has_metadata_track = false;
  uint32_t track_count = MP4GetNumberOfTracks(read_handle, nullptr, 0);
  for (uint32_t index = 0; index < track_count; ++index) {
    MP4TrackId track_id = MP4FindTrackId(read_handle, index, nullptr, 0);
    const char* track_type = MP4GetTrackType(read_handle, track_id);
    if (track_type && strcmp(track_type, kVideoMetadataTrackType) == 0) {
      has_metadata_track = true;
      break;
    }
  }
  MP4Close(read_handle);

  if (!has_metadata_track) {
    return true;
  }

  MP4FileHandle file_handle = MP4Modify(file_name.c_str());
  if (file_handle == MP4_INVALID_FILE_HANDLE) {
    message_handler->ReportMessage(Message::kStdLibError, file_name);
    return false;
  }

  bool deleted = false;
  track_count = MP4GetNumberOfTracks(file_handle, nullptr, 0);
  for (int index = (int)track_count - 1; index >= 0; --index) {
    MP4TrackId track_id = MP4FindTrackId(file_handle, index, nullptr, 0);
    const char* track_type = MP4GetTrackType(file_handle, track_id);
    if (track_type && strcmp(track_type, kVideoMetadataTrackType) == 0) {
      MP4DeleteTrack(file_handle, track_id);
      deleted = true;
    }
  }

  MP4Close(file_handle);

  if (deleted) {
    if (!MP4Optimize(file_name.c_str(), nullptr)) {
      errno = EIO;
      stringstream ss;
      ss << "Can't optimize file after stripping metadata: " << file_name;
      message_handler->ReportMessage(Message::kStdLibError, ss.str());
      return false;
    }
  }
  return true;
}

}  // namespace motion_photo
}  // namespace libmotionphoto
