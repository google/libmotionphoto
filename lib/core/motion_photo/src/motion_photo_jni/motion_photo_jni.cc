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

#include <fcntl.h>
#include <jni.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "image_io/base/data_destination.h"
#include "image_io/base/data_segment.h"
#include "image_io/base/data_segment_data_source.h"
#include "image_io/base/data_source.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/types.h"
#include "image_io/utils/file_utils.h"
#include "image_io/utils/string_outputter.h"
#include "metadata_collection.pb.h"  // NOLINT
#include "motion_photo/camera_metadata.h"
#include "motion_photo/google_motion_photo_provider.h"
#include "motion_photo/metadata_engine.h"
#include "motion_photo/motion_photo.h"
#include "motion_photo/motion_photo_parser.h"
#include "motion_photo/motion_photo_source.h"
#include "motion_photo/motion_photo_utils.h"
#include "motion_photo_builder/function.h"
#include "motion_photo_checker/function.h"
#include "motion_photo_extractor/function.h"
#include "motion_photo_jni/jni_utils.h"

namespace {

using libmotionphoto::image_io::StringOutputter;
using libmotionphoto::jni::ConvertJStringArray;
using libmotionphoto::jni::GetStringField;
using libmotionphoto::jni::JniOutputter;
using libmotionphoto::jni::ScopedByteArrayElements;
using libmotionphoto::jni::ScopedLocalRef;
using libmotionphoto::jni::ScopedUtfChars;
using libmotionphoto::jni::ThrowOutOfMemoryError;
using libmotionphoto::jni::ThrowRuntimeException;

// Returns kHeic for ISO BMFF images, which start with an 'ftyp' box, and kJpeg
// otherwise. The metadata engine parses HEIC and AVIF the same way.
libmotionphoto::motion_photo::FileType SniffFileType(const uint8_t* data,
                                                     size_t size) {
  if (data != nullptr && size >= 8 && data[4] == 'f' && data[5] == 't' &&
      data[6] == 'y' && data[7] == 'p') {
    return libmotionphoto::motion_photo::FileType::kHeic;
  }
  return libmotionphoto::motion_photo::FileType::kJpeg;
}

// Lazy random-access DataSource backed by pread() on an open file descriptor.
// Reads 64 KB chunks on demand over the full [0, total_length_) range without
// buffering the entire file in memory.
class FdDataSource : public libmotionphoto::image_io::DataSource {
 public:
  FdDataSource(int fd, int64_t base_offset, int64_t total_length)
      : fd_(fd), base_offset_(base_offset), total_length_(total_length) {}

  ~FdDataSource() override = default;

  void Reset() override { current_data_segment_.reset(); }

  std::shared_ptr<libmotionphoto::image_io::DataSegment> GetDataSegment(
      size_t begin, size_t min_size) override {
    if (current_data_segment_ && current_data_segment_->Contains(begin)) {
      size_t remaining = current_data_segment_->GetEnd() - begin;
      if (remaining >= min_size) {
        return current_data_segment_;
      }
    }
    current_data_segment_ = Read(begin, min_size);
    return current_data_segment_;
  }

  TransferDataResult TransferData(
      const libmotionphoto::image_io::DataRange& data_range, size_t best_size,
      libmotionphoto::image_io::DataDestination* data_destination) override {
    namespace ii = libmotionphoto::image_io;
    bool data_transferred = false;
    ii::DataDestination::TransferStatus status =
        ii::DataDestination::kTransferDone;
    if (data_destination && data_range.IsValid() && fd_ >= 0) {
      size_t min_size = std::min(data_range.GetLength(), best_size);
      if (current_data_segment_ &&
          current_data_segment_->GetLength() >= min_size &&
          current_data_segment_->GetDataRange().Contains(data_range)) {
        status = data_destination->Transfer(data_range, *current_data_segment_);
        data_transferred = true;
      } else {
        size_t chunk_size = std::max(min_size, static_cast<size_t>(64 * 1024));
        for (size_t begin = data_range.GetBegin(); begin < data_range.GetEnd();
             begin += chunk_size) {
          size_t end = std::min(data_range.GetEnd(), begin + chunk_size);
          auto data_segment = Read(begin, end - begin);
          if (data_segment && data_segment->GetLength() > 0) {
            status = data_destination->Transfer(data_segment->GetDataRange(),
                                                *data_segment);
            data_transferred = true;
          }
          if (status != ii::DataDestination::kTransferOk || !data_segment ||
              data_segment->GetLength() == 0) {
            break;
          }
        }
      }
    }
    if (data_transferred) {
      return status == ii::DataDestination::kTransferError
                 ? kTransferDataError
                 : kTransferDataSuccess;
    }
    return data_destination ? kTransferDataNone : kTransferDataError;
  }

 private:
  std::shared_ptr<libmotionphoto::image_io::DataSegment> Read(size_t begin,
                                                              size_t min_size) {
    namespace ii = libmotionphoto::image_io;
    if (fd_ < 0 || total_length_ <= 0 ||
        begin >= static_cast<size_t>(total_length_)) {
      return nullptr;
    }
    size_t chunk_size = std::max(min_size, static_cast<size_t>(64 * 1024));
    size_t to_read =
        std::min(chunk_size, static_cast<size_t>(total_length_) - begin);
    if (to_read == 0) {
      return nullptr;
    }

    auto buffer = std::make_shared<std::vector<ii::Byte>>(to_read);
    size_t total_bytes_read = 0;
    while (total_bytes_read < to_read) {
      ssize_t bytes = pread(
          fd_, buffer->data() + total_bytes_read, to_read - total_bytes_read,
          static_cast<off_t>(base_offset_ + begin + total_bytes_read));
      if (bytes <= 0) {
        if (bytes < 0 && errno == EINTR) continue;
        break;
      }
      total_bytes_read += static_cast<size_t>(bytes);
    }

    if (total_bytes_read == 0) {
      return nullptr;
    }

    auto segment = ii::DataSegment::Create(
        ii::DataRange(begin, begin + total_bytes_read), buffer->data(),
        ii::DataSegment::BufferDispositionPolicy::kDontDelete);
    return std::shared_ptr<ii::DataSegment>(
        segment.get(), [segment, buffer](ii::DataSegment*) {});
  }

  int fd_ = -1;
  int64_t base_offset_ = 0;
  int64_t total_length_ = 0;
  std::shared_ptr<libmotionphoto::image_io::DataSegment> current_data_segment_;
};

jbyteArray ParseMetadataFromFd(JNIEnv* env, int fd, int64_t offset,
                               int64_t length, bool owns_fd) {
  struct ScopedFd {
    int fd;
    bool owns;
    ~ScopedFd() {
      if (owns && fd >= 0) {
        close(fd);
      }
    }
  } fd_closer{fd, owns_fd};

  if (env == nullptr || fd < 0 || offset < 0 || length < 0) {
    return nullptr;
  }

  struct stat sb;
  if (fstat(fd, &sb) != 0 || sb.st_size <= 0) {
    return nullptr;
  }

  int64_t slice_length = length;
  if (slice_length == 0) {
    if (offset >= sb.st_size) {
      return nullptr;
    }
    slice_length = sb.st_size - offset;
  } else {
    if (slice_length > sb.st_size || offset > sb.st_size - slice_length) {
      return nullptr;
    }
  }
  if (slice_length <= 0) {
    return nullptr;
  }

  namespace mp = libmotionphoto::motion_photo;
  namespace ii = libmotionphoto::image_io;

  ii::MessageHandler message_handler;
  FdDataSource data_source(fd, offset, slice_length);

  mp::MetadataEngine engine(&message_handler);
  engine.RegisterProvider(std::make_unique<mp::GoogleMotionPhotoProvider>());

  mp::FileType file_type = mp::FileType::kJpeg;
  if (std::shared_ptr<ii::DataSegment> header_segment =
          data_source.GetDataSegment(0, 8)) {
    file_type = SniffFileType(header_segment->GetBuffer(0),
                              header_segment->GetLength());
  }
  mp::MetadataCollection collection =
      engine.Parse(&data_source, static_cast<size_t>(slice_length), file_type);

  for (int i = 0; i < collection.blocks_size(); ++i) {
    if (collection.blocks(i).format_identifier() == "container.trailer") {
      collection.mutable_blocks(i)->clear_raw_bytes();
    }
  }

  std::vector<uint8_t> proto_bytes(collection.ByteSizeLong());
  if (!collection.SerializeToArray(proto_bytes.data(), proto_bytes.size())) {
    return nullptr;
  }

  jbyteArray j_array = env->NewByteArray(proto_bytes.size());
  if (j_array == nullptr) {
    return nullptr;
  }
  env->SetByteArrayRegion(j_array, 0, proto_bytes.size(),
                          reinterpret_cast<const jbyte*>(proto_bytes.data()));

  return j_array;
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL
Java_com_google_libmotionphoto_motionphoto_MotionPhotoJni_builder(
    JNIEnv* env, jclass clazz, jobject jparams, jobject jcallback) {
  if (env == nullptr || jparams == nullptr) {
    return -1;
  }
  try {
    ScopedLocalRef<jclass> params_class(env, env->GetObjectClass(jparams));
    if (!params_class.ok()) {
      return -1;
    }

    namespace mp = libmotionphoto::motion_photo;
    mp::MotionPhotoBuilderParams params;
    params.primary_image_file_name =
        GetStringField(env, jparams, params_class.get(), "primaryImage");
    params.primary_video_file_name =
        GetStringField(env, jparams, params_class.get(), "primaryVideo");
    params.moments_video_file_name =
        GetStringField(env, jparams, params_class.get(), "momentsVideo");
    params.moments_xmp_file_name =
        GetStringField(env, jparams, params_class.get(), "momentsXmp");
    params.output_image_file_name =
        GetStringField(env, jparams, params_class.get(), "outputImage");
    params.working_video_file_name =
        GetStringField(env, jparams, params_class.get(), "workingVideo");
    params.optimized_video_file_name =
        GetStringField(env, jparams, params_class.get(), "optimizedVideo");

    jfieldID ts_fid =
        env->GetFieldID(params_class.get(), "presentationTimestampUs", "J");
    if (ts_fid != nullptr) {
      params.presentation_timestamp_us = env->GetLongField(jparams, ts_fid);
    } else {
      env->ExceptionClear();
    }

    StringOutputter callback = [](const std::string&) {};
    std::unique_ptr<JniOutputter> jni_outputter;
    if (jcallback != nullptr) {
      jni_outputter = std::make_unique<JniOutputter>(env, jcallback);
      JniOutputter* raw_outputter = jni_outputter.get();
      callback = [raw_outputter](const std::string& message) {
        raw_outputter->Output(message);
      };
    }

    return mp::BuildMotionPhoto(params, callback);
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return -1;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return -1;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in builder");
    return -1;
  }
}

JNIEXPORT jint JNICALL
Java_com_google_libmotionphoto_motionphoto_MotionPhotoJni_builderFromMemory(
    JNIEnv* env, jclass clazz, jbyteArray jimage_bytes, jbyteArray jvideo_bytes,
    jstring jmoments_xmp, jstring joutput_image_file, jobject jcallback) {
  if (env == nullptr || jimage_bytes == nullptr || jvideo_bytes == nullptr ||
      joutput_image_file == nullptr) {
    return -1;
  }
  try {
    jsize image_len = env->GetArrayLength(jimage_bytes);
    ScopedByteArrayElements image_buf(env, jimage_bytes);
    if (!image_buf.ok()) return -1;

    jsize video_len = env->GetArrayLength(jvideo_bytes);
    ScopedByteArrayElements video_buf(env, jvideo_bytes);
    if (!video_buf.ok()) return -1;

    ScopedUtfChars out_file_chars(env, joutput_image_file);
    if (!out_file_chars.ok()) {
      return -1;
    }
    std::string output_image_file(out_file_chars.c_str());

    std::string moments_xmp;
    if (jmoments_xmp != nullptr) {
      ScopedUtfChars xmp_chars(env, jmoments_xmp);
      if (!xmp_chars.ok()) {
        return -1;
      }
      moments_xmp = xmp_chars.c_str();
    }

    namespace mp = libmotionphoto::motion_photo;
    mp::MotionPhotoBuilderMemoryParams params;
    params.primary_image_bytes =
        reinterpret_cast<const uint8_t*>(image_buf.get());
    params.primary_image_size = static_cast<size_t>(image_len);
    params.primary_video_bytes =
        reinterpret_cast<const uint8_t*>(video_buf.get());
    params.primary_video_size = static_cast<size_t>(video_len);
    params.moments_xmp = moments_xmp;
    params.output_image_file_name = output_image_file;

    StringOutputter callback = [](const std::string&) {};
    std::unique_ptr<JniOutputter> jni_outputter;
    if (jcallback != nullptr) {
      jni_outputter = std::make_unique<JniOutputter>(env, jcallback);
      JniOutputter* raw_outputter = jni_outputter.get();
      callback = [raw_outputter](const std::string& message) {
        raw_outputter->Output(message);
      };
    }

    return mp::BuildMotionPhotoFromMemory(params, callback);
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return -1;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return -1;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in builderFromMemory");
    return -1;
  }
}

JNIEXPORT jbyteArray JNICALL
Java_com_google_libmotionphoto_motionphoto_MotionPhotoJni_builderToByteArray(
    JNIEnv* env, jclass clazz, jbyteArray jimage_bytes, jbyteArray jvideo_bytes,
    jstring jmoments_xmp, jlong jtimestamp_us) {
  if (env == nullptr || jimage_bytes == nullptr || jvideo_bytes == nullptr) {
    return nullptr;
  }
  try {
    jsize image_len = env->GetArrayLength(jimage_bytes);
    ScopedByteArrayElements image_buf(env, jimage_bytes);
    if (!image_buf.ok()) return nullptr;

    jsize video_len = env->GetArrayLength(jvideo_bytes);
    ScopedByteArrayElements video_buf(env, jvideo_bytes);
    if (!video_buf.ok()) return nullptr;

    std::string moments_xmp;
    if (jmoments_xmp != nullptr) {
      ScopedUtfChars xmp_chars(env, jmoments_xmp);
      if (!xmp_chars.ok()) {
        return nullptr;
      }
      moments_xmp = xmp_chars.c_str();
    }

    namespace mp = libmotionphoto::motion_photo;
    auto img_source = mp::MediaSource::FromMemory(
        reinterpret_cast<const uint8_t*>(image_buf.get()),
        static_cast<size_t>(image_len));
    auto vid_source = mp::MediaSource::FromMemory(
        reinterpret_cast<const uint8_t*>(video_buf.get()),
        static_cast<size_t>(video_len));

    std::vector<uint8_t> output_buffer;
    auto mem_sink = mp::DataSink::ToMemory(&output_buffer);

    mp::MotionPhotoStreamParams stream_params;
    stream_params.primary_image = std::move(img_source);
    stream_params.primary_video = std::move(vid_source);
    stream_params.moments_xmp = moments_xmp;
    stream_params.presentation_timestamp_us =
        static_cast<int64_t>(jtimestamp_us);

    int res = mp::BuildMotionPhotoDirect(stream_params, mem_sink.get(),
                                         [](const std::string&) {});

    if (res != 0 || output_buffer.empty()) {
      return nullptr;
    }

    jbyteArray result = env->NewByteArray(output_buffer.size());
    if (result != nullptr) {
      env->SetByteArrayRegion(
          result, 0, output_buffer.size(),
          reinterpret_cast<const jbyte*>(output_buffer.data()));
    }
    return result;
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return nullptr;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return nullptr;
  } catch (...) {
    ThrowRuntimeException(env,
                          "Unknown native exception in builderToByteArray");
    return nullptr;
  }
}

JNIEXPORT jint JNICALL
Java_com_google_libmotionphoto_motionphoto_MotionPhotoJni_checker(
    JNIEnv* env, jclass clazz, jstring jmotion_photo_file, jobject jcallback) {
  if (env == nullptr || jmotion_photo_file == nullptr) {
    return -1;
  }
  try {
    ScopedUtfChars file_str(env, jmotion_photo_file);
    if (!file_str.ok()) {
      return -1;
    }
    std::string motion_photo_file(file_str.c_str());

    StringOutputter callback = [](const std::string&) {};
    std::unique_ptr<JniOutputter> jni_outputter;
    if (jcallback != nullptr) {
      jni_outputter = std::make_unique<JniOutputter>(env, jcallback);
      JniOutputter* raw_outputter = jni_outputter.get();
      callback = [raw_outputter](const std::string& message) {
        raw_outputter->Output(message);
      };
    }

    namespace mp = libmotionphoto::motion_photo;
    return mp::CheckMotionPhoto(motion_photo_file, callback);
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return -1;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return -1;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in checker");
    return -1;
  }
}

JNIEXPORT jint JNICALL
Java_com_google_libmotionphoto_motionphoto_MotionPhotoJni_extractor(
    JNIEnv* env, jclass clazz, jobject jparams, jobject jcallback) {
  if (env == nullptr || jparams == nullptr) {
    return -1;
  }
  try {
    ScopedLocalRef<jclass> params_class(env, env->GetObjectClass(jparams));
    if (!params_class.ok()) {
      return -1;
    }

    namespace mp = libmotionphoto::motion_photo;
    mp::MotionPhotoExtractorParams params;
    params.motion_photo_file_name =
        GetStringField(env, jparams, params_class.get(), "motionPhotoFile");
    params.primary_image_file_name_output =
        GetStringField(env, jparams, params_class.get(), "primaryImageOutput");
    params.video_file_name_output =
        GetStringField(env, jparams, params_class.get(), "videoOutput");
    params.metadata_file_name_output =
        GetStringField(env, jparams, params_class.get(), "metadataOutput");

    StringOutputter callback = [](const std::string&) {};
    std::unique_ptr<JniOutputter> jni_outputter;
    if (jcallback != nullptr) {
      jni_outputter = std::make_unique<JniOutputter>(env, jcallback);
      JniOutputter* raw_outputter = jni_outputter.get();
      callback = [raw_outputter](const std::string& message) {
        raw_outputter->Output(message);
      };
    }

    return mp::ExtractMotionPhoto(params, callback);
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return -1;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return -1;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in extractor");
    return -1;
  }
}

JNIEXPORT jbyteArray JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_parseMetadata(
    JNIEnv* env, jclass clazz, jstring jfile_path) {
  if (env == nullptr || jfile_path == nullptr) {
    return nullptr;
  }
  try {
    ScopedUtfChars file_path_chars(env, jfile_path);
    if (!file_path_chars.ok()) {
      return nullptr;
    }
    int fd = open(file_path_chars.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      return nullptr;
    }
    return ParseMetadataFromFd(env, fd, /*offset=*/0, /*length=*/0,
                               /*owns_fd=*/true);
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return nullptr;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return nullptr;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in parseMetadata");
    return nullptr;
  }
}

JNIEXPORT jbyteArray JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_parseMetadataFd__IJJ(
    JNIEnv* env, jclass clazz, jint jfd, jlong joffset, jlong jlength) {
  int fd = jfd;
  if (env == nullptr || fd < 0 || joffset < 0 || jlength < 0) {
    return nullptr;
  }
  try {
    return ParseMetadataFromFd(env, fd, joffset, jlength, /*owns_fd=*/false);
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return nullptr;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return nullptr;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in parseMetadataFd");
    return nullptr;
  }
}

JNIEXPORT jbyteArray JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_parseMetadataFd__I(
    JNIEnv* env, jclass clazz, jint jfd) {
  return Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_parseMetadataFd__IJJ(
      env, clazz, jfd, 0, 0);
}

JNIEXPORT jbyteArray JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_parseMetadataFd(
    JNIEnv* env, jclass clazz, jint jfd) {
  return Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_parseMetadataFd__IJJ(
      env, clazz, jfd, 0, 0);
}

JNIEXPORT jboolean JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_isMotionPhoto(
    JNIEnv* env, jclass clazz, jbyteArray serialized_collection,
    jboolean disable_3p_plugins, jobjectArray enabled_3p_plugins) {
  if (env == nullptr || serialized_collection == nullptr) {
    return JNI_FALSE;
  }
  try {
    jsize len = env->GetArrayLength(serialized_collection);
    ScopedByteArrayElements buf(env, serialized_collection);
    if (!buf.ok()) {
      return JNI_FALSE;
    }
    std::string proto_bytes(reinterpret_cast<const char*>(buf.get()), len);

    namespace mp = libmotionphoto::motion_photo;
    mp::MetadataCollection collection;
    if (!collection.ParseFromString(proto_bytes)) {
      return JNI_FALSE;
    }

    mp::HandlerOptions options;
    options.disable_3p_plugins = (disable_3p_plugins == JNI_TRUE);
    options.enabled_3p_plugins = ConvertJStringArray(env, enabled_3p_plugins);

    return mp::MetadataEngine::IsMotionPhoto(collection, options) ? JNI_TRUE
                                                                  : JNI_FALSE;
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return JNI_FALSE;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return JNI_FALSE;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in isMotionPhoto");
    return JNI_FALSE;
  }
}

JNIEXPORT jstring JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_extractAgtm(
    JNIEnv* env, jclass clazz, jstring jfile_path) {
  if (env == nullptr || jfile_path == nullptr) {
    return nullptr;
  }
  try {
    ScopedUtfChars file_path_chars(env, jfile_path);
    if (!file_path_chars.ok()) {
      return nullptr;
    }
    std::string file_path(file_path_chars.c_str());

    namespace mp = libmotionphoto::motion_photo;
    namespace ii = libmotionphoto::image_io;
    ii::MessageHandler message_handler;
    mp::MetadataEngine engine(&message_handler);

    int64_t target_ts = 0;
    mp::FileType file_type = mp::GetFileTypeFromFileName(file_path);
    std::shared_ptr<ii::DataSegment> data_segment =
        ii::ReadEntireFile(file_path, &message_handler);
    if (data_segment && data_segment->GetLength() > 0) {
      ii::DataSegmentDataSource data_source(data_segment);
      mp::MetadataCollection collection =
          engine.Parse(&data_source, data_segment->GetLength(), file_type);
      mp::MotionPhoto photo;
      mp::MotionPhotoParser parser(&message_handler);
      size_t bytes_parsed = 0;
      for (const auto& block : collection.blocks()) {
        if (block.type() == mp::BLOCK_TYPE_XMP ||
            block.format_identifier() == "XMP") {
          std::string_view xml(block.raw_bytes().data(),
                               block.raw_bytes().size());
          if (parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
            const auto& camera = photo.GetCameraMetadata();
            if (camera.motion_photo_presentation_timestamp_us.WasAssigned() &&
                camera.motion_photo_presentation_timestamp_us.IsValid()) {
              target_ts =
                  camera.motion_photo_presentation_timestamp_us.GetValue();
            }
            break;
          }
        }
      }
    }

    std::string result_json =
        engine.ExtractAgtmAtTimestamp(file_path, target_ts);
    return env->NewStringUTF(result_json.c_str());
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return nullptr;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return nullptr;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in extractAgtm");
    return nullptr;
  }
}

JNIEXPORT jstring JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_extractAgtmFromMemory(
    JNIEnv* env, jclass clazz, jbyteArray jdata) {
  if (env == nullptr || jdata == nullptr) {
    return nullptr;
  }
  try {
    jsize len = env->GetArrayLength(jdata);
    if (len <= 0) {
      return nullptr;
    }
    ScopedByteArrayElements buf(env, jdata);
    if (!buf.ok()) {
      return nullptr;
    }
    const uint8_t* data = reinterpret_cast<const uint8_t*>(buf.get());

    namespace mp = libmotionphoto::motion_photo;
    namespace ii = libmotionphoto::image_io;
    ii::MessageHandler message_handler;
    mp::MetadataEngine engine(&message_handler);

    int64_t target_ts = 0;
    const mp::FileType file_type =
        SniffFileType(data, static_cast<size_t>(len));

    auto data_segment = ii::DataSegment::Create(
        ii::DataRange(0, len), data,
        ii::DataSegment::BufferDispositionPolicy::kDontDelete);
    ii::DataSegmentDataSource data_source(data_segment);
    mp::MetadataCollection collection =
        engine.Parse(&data_source, len, file_type);
    mp::MotionPhoto photo;
    mp::MotionPhotoParser parser(&message_handler);
    size_t bytes_parsed = 0;
    for (const auto& block : collection.blocks()) {
      if (block.type() == mp::BLOCK_TYPE_XMP ||
          block.format_identifier() == "XMP") {
        std::string_view xml(block.raw_bytes().data(),
                             block.raw_bytes().size());
        if (parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
          const auto& camera = photo.GetCameraMetadata();
          if (camera.motion_photo_presentation_timestamp_us.WasAssigned() &&
              camera.motion_photo_presentation_timestamp_us.IsValid()) {
            target_ts =
                camera.motion_photo_presentation_timestamp_us.GetValue();
          }
          break;
        }
      }
    }

    std::string result_json =
        engine.ExtractAgtmAtTimestampMemory(data, len, target_ts);
    return env->NewStringUTF(result_json.c_str());
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return nullptr;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return nullptr;
  } catch (...) {
    ThrowRuntimeException(env,
                          "Unknown native exception in extractAgtmFromMemory");
    return nullptr;
  }
}

JNIEXPORT jstring JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_extractAgtmFd__IJJ(
    JNIEnv* env, jclass clazz, jint fd, jlong joffset, jlong jlength) {
  if (env == nullptr || fd < 0 || joffset < 0 || jlength < 0) {
    return nullptr;
  }
  try {
    size_t file_size = 0;
    if (jlength > 0) {
      file_size = static_cast<size_t>(jlength);
    } else {
      struct stat sb;
      if (fstat(fd, &sb) != -1 && sb.st_size > 0) {
        file_size = static_cast<size_t>(sb.st_size);
      }
    }
    if (file_size == 0) {
      return nullptr;
    }

    std::vector<char> buffer(file_size);
    size_t bytes_read = 0;
    off_t read_offset = static_cast<off_t>(joffset);
    while (bytes_read < file_size) {
      ssize_t r = pread(fd, buffer.data() + bytes_read, file_size - bytes_read,
                        read_offset + bytes_read);
      if (r <= 0) {
        if (r < 0 && errno == EINTR) continue;
        break;
      }
      bytes_read += r;
    }

    if (bytes_read < file_size) {
      return nullptr;
    }

    namespace mp = libmotionphoto::motion_photo;
    namespace ii = libmotionphoto::image_io;
    ii::MessageHandler message_handler;
    mp::MetadataEngine engine(&message_handler);

    int64_t target_ts = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer.data());
    const mp::FileType file_type = SniffFileType(data, bytes_read);
    auto data_segment = ii::DataSegment::Create(
        ii::DataRange(0, bytes_read), data,
        ii::DataSegment::BufferDispositionPolicy::kDontDelete);
    ii::DataSegmentDataSource data_source(data_segment);
    mp::MetadataCollection collection =
        engine.Parse(&data_source, bytes_read, file_type);
    mp::MotionPhoto photo;
    mp::MotionPhotoParser parser(&message_handler);
    size_t bytes_parsed = 0;
    for (const auto& block : collection.blocks()) {
      if (block.type() == mp::BLOCK_TYPE_XMP ||
          block.format_identifier() == "XMP") {
        std::string_view xml(block.raw_bytes().data(),
                             block.raw_bytes().size());
        if (parser.ParseXmpString(xml, &photo, &bytes_parsed)) {
          const auto& camera = photo.GetCameraMetadata();
          if (camera.motion_photo_presentation_timestamp_us.WasAssigned() &&
              camera.motion_photo_presentation_timestamp_us.IsValid()) {
            target_ts =
                camera.motion_photo_presentation_timestamp_us.GetValue();
          }
          break;
        }
      }
    }

    std::string result_json =
        engine.ExtractAgtmAtTimestampMemory(data, bytes_read, target_ts);
    return env->NewStringUTF(result_json.c_str());
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return nullptr;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return nullptr;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in extractAgtmFd");
    return nullptr;
  }
}

JNIEXPORT jstring JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_extractAgtmFd__I(
    JNIEnv* env, jclass clazz, jint fd) {
  return Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_extractAgtmFd__IJJ(
      env, clazz, fd, 0, 0);
}

JNIEXPORT jstring JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_extractAgtmFd(
    JNIEnv* env, jclass clazz, jint fd) {
  return Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_extractAgtmFd__IJJ(
      env, clazz, fd, 0, 0);
}

JNIEXPORT jstring JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_extractAgtmAtTimestamp(
    JNIEnv* env, jclass clazz, jstring jfile_path, jlong jtimestamp_us) {
  if (env == nullptr || jfile_path == nullptr) {
    return nullptr;
  }
  try {
    ScopedUtfChars file_path_chars(env, jfile_path);
    if (!file_path_chars.ok()) {
      return nullptr;
    }
    std::string file_path(file_path_chars.c_str());

    namespace mp = libmotionphoto::motion_photo;
    namespace ii = libmotionphoto::image_io;
    ii::MessageHandler message_handler;
    mp::MetadataEngine engine(&message_handler);
    std::string result_json = engine.ExtractAgtmAtTimestamp(
        file_path, static_cast<int64_t>(jtimestamp_us));

    return env->NewStringUTF(result_json.c_str());
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return nullptr;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return nullptr;
  } catch (...) {
    ThrowRuntimeException(env,
                          "Unknown native exception in extractAgtmAtTimestamp");
    return nullptr;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_google_libmotionphoto_motionphoto_MetadataEngineJni_extractMetadata(
    JNIEnv* env, jclass clazz, jstring jinput_file_path,
    jstring joutput_metadata_file_path) {
  if (env == nullptr || jinput_file_path == nullptr ||
      joutput_metadata_file_path == nullptr) {
    return JNI_FALSE;
  }
  try {
    ScopedUtfChars in_chars(env, jinput_file_path);
    if (!in_chars.ok()) {
      return JNI_FALSE;
    }
    std::string input_file_path(in_chars.c_str());

    ScopedUtfChars out_chars(env, joutput_metadata_file_path);
    if (!out_chars.ok()) {
      return JNI_FALSE;
    }
    std::string output_metadata_file_path(out_chars.c_str());

    namespace mp = libmotionphoto::motion_photo;
    namespace ii = libmotionphoto::image_io;
    ii::MessageHandler message_handler;
    mp::MetadataEngine engine(&message_handler);
    bool success =
        engine.ExtractMetadata(input_file_path, output_metadata_file_path);

    return success ? JNI_TRUE : JNI_FALSE;
  } catch (const std::bad_alloc& e) {
    ThrowOutOfMemoryError(env, e.what());
    return JNI_FALSE;
  } catch (const std::exception& e) {
    ThrowRuntimeException(env, e.what());
    return JNI_FALSE;
  } catch (...) {
    ThrowRuntimeException(env, "Unknown native exception in extractMetadata");
    return JNI_FALSE;
  }
}
}
