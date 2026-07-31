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

#ifndef MOTION_PHOTO_JNI_JNI_UTILS_H_
#define MOTION_PHOTO_JNI_JNI_UTILS_H_

#include <jni.h>

#include <string>
#include <vector>

namespace libmotionphoto {
namespace jni {

// RAII wrapper for local JNI object references.
template <typename T>
class ScopedLocalRef {
 public:
  ScopedLocalRef(JNIEnv* env, T ref) : env_(env), ref_(ref) {}
  ~ScopedLocalRef() {
    if (env_ != nullptr && ref_ != nullptr) {
      env_->DeleteLocalRef(ref_);
    }
  }

  ScopedLocalRef(const ScopedLocalRef&) = delete;
  ScopedLocalRef& operator=(const ScopedLocalRef&) = delete;

  T get() const { return ref_; }
  T release() {
    T temp = ref_;
    ref_ = nullptr;
    return temp;
  }
  bool ok() const { return ref_ != nullptr; }

 private:
  JNIEnv* env_;
  T ref_;
};

// RAII wrapper for JNI GetStringUTFChars / ReleaseStringUTFChars.
class ScopedUtfChars {
 public:
  ScopedUtfChars(JNIEnv* env, jstring jstr)
      : env_(env), jstr_(jstr), str_(nullptr) {
    if (env_ != nullptr && jstr_ != nullptr) {
      str_ = env_->GetStringUTFChars(jstr_, nullptr);
    }
  }

  ~ScopedUtfChars() {
    if (str_ != nullptr && env_ != nullptr && jstr_ != nullptr) {
      env_->ReleaseStringUTFChars(jstr_, str_);
    }
  }

  ScopedUtfChars(const ScopedUtfChars&) = delete;
  ScopedUtfChars& operator=(const ScopedUtfChars&) = delete;

  const char* c_str() const { return str_; }
  bool ok() const { return str_ != nullptr; }

 private:
  JNIEnv* env_;
  jstring jstr_;
  const char* str_;
};

// RAII wrapper for JNI GetByteArrayElements / ReleaseByteArrayElements.
class ScopedByteArrayElements {
 public:
  ScopedByteArrayElements(JNIEnv* env, jbyteArray array, jint mode = JNI_ABORT)
      : env_(env), array_(array), mode_(mode), elems_(nullptr) {
    if (env_ != nullptr && array_ != nullptr) {
      elems_ = env_->GetByteArrayElements(array_, nullptr);
    }
  }

  ~ScopedByteArrayElements() {
    if (elems_ != nullptr && env_ != nullptr && array_ != nullptr) {
      env_->ReleaseByteArrayElements(array_, elems_, mode_);
    }
  }

  ScopedByteArrayElements(const ScopedByteArrayElements&) = delete;
  ScopedByteArrayElements& operator=(const ScopedByteArrayElements&) = delete;

  const jbyte* get() const { return elems_; }
  bool ok() const { return elems_ != nullptr; }

 private:
  JNIEnv* env_;
  jbyteArray array_;
  jint mode_;
  jbyte* elems_;
};

// Callback outputter that routes C++ output messages to a Java callback object.
class JniOutputter {
 public:
  JniOutputter(JNIEnv* env, jobject callback_obj)
      : env_(env), callback_obj_(callback_obj) {
    if (env_ != nullptr && callback_obj_ != nullptr) {
      ScopedLocalRef<jclass> clazz(env_, env_->GetObjectClass(callback_obj_));
      if (clazz.ok()) {
        method_id_ =
            env_->GetMethodID(clazz.get(), "onOutput", "(Ljava/lang/String;)V");
        if (method_id_ == nullptr) {
          env_->ExceptionClear();
        }
      }
    }
  }

  void Output(const std::string& message) {
    if (env_ != nullptr && callback_obj_ != nullptr && method_id_ != nullptr) {
      ScopedLocalRef<jstring> jmsg(env_, env_->NewStringUTF(message.c_str()));
      if (jmsg.ok()) {
        env_->CallVoidMethod(callback_obj_, method_id_, jmsg.get());
      }
    }
  }

 private:
  JNIEnv* env_;
  jobject callback_obj_;
  jmethodID method_id_ = nullptr;
};

inline void ThrowException(JNIEnv* env, const char* exception_class,
                           const char* message) {
  if (env == nullptr || exception_class == nullptr) return;
  ScopedLocalRef<jclass> clazz(env, env->FindClass(exception_class));
  if (clazz.ok()) {
    env->ThrowNew(clazz.get(), message != nullptr ? message : "");
  }
}

inline void ThrowOutOfMemoryError(JNIEnv* env, const char* message) {
  ThrowException(env, "java/lang/OutOfMemoryError", message);
}

inline void ThrowRuntimeException(JNIEnv* env, const char* message) {
  ThrowException(env, "java/lang/RuntimeException", message);
}

inline std::string GetStringField(JNIEnv* env, jobject obj, jclass clazz,
                                  const char* field_name) {
  if (env == nullptr || obj == nullptr || clazz == nullptr ||
      field_name == nullptr) {
    return "";
  }
  jfieldID fid = env->GetFieldID(clazz, field_name, "Ljava/lang/String;");
  if (fid == nullptr) {
    env->ExceptionClear();
    return "";
  }
  ScopedLocalRef<jstring> jstr(
      env, static_cast<jstring>(env->GetObjectField(obj, fid)));
  if (!jstr.ok()) return "";
  ScopedUtfChars str(env, jstr.get());
  return str.ok() ? str.c_str() : "";
}

inline std::vector<std::string> ConvertJStringArray(JNIEnv* env,
                                                    jobjectArray array) {
  std::vector<std::string> result;
  if (env == nullptr || array == nullptr) {
    return result;
  }
  jsize len = env->GetArrayLength(array);
  result.reserve(static_cast<size_t>(len));
  for (jsize i = 0; i < len; ++i) {
    ScopedLocalRef<jstring> jstr(
        env, static_cast<jstring>(env->GetObjectArrayElement(array, i)));
    if (jstr.ok()) {
      ScopedUtfChars str(env, jstr.get());
      if (str.ok()) {
        result.push_back(str.c_str());
      }
    }
  }
  return result;
}

}  // namespace jni
}  // namespace libmotionphoto

#endif  // MOTION_PHOTO_JNI_JNI_UTILS_H_
