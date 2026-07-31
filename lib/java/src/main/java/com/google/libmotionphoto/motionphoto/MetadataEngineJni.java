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

package com.google.libmotionphoto.motionphoto;

/** JNI interface for the Metadata Engine. */
class MetadataEngineJni {
  private MetadataEngineJni() {}

  static {
    System.loadLibrary("motion_photo_jni");
  }

  /**
   * Parses metadata from the given image file path.
   *
   * @param filePath The path to the image file.
   * @return Serialized MetadataCollection proto bytes, or null on error.
   */
  static native byte[] parseMetadata(String filePath);

  /**
   * Parses metadata from the given file descriptor.
   *
   * @param fd The file descriptor of the image file.
   * @return Serialized MetadataCollection proto bytes, or null on error.
   */
  static native byte[] parseMetadataFd(int fd);

  static native byte[] parseMetadataFd(int fd, long offset, long length);

  /**
   * Helper to check if the serialized MetadataCollection represents a motion photo.
   *
   * @param serializedCollection Serialized MetadataCollection proto bytes.
   * @return true if it is a motion photo, false otherwise.
   */
  static boolean isMotionPhoto(byte[] serializedCollection) {
    return isMotionPhoto(serializedCollection, false, new String[0]);
  }

  static native boolean isMotionPhoto(
      byte[] serializedCollection, boolean disable3pPlugins, String[] enabled3pPlugins);

  /**
   * Extracts AGTM (SMPTE ST 2094-50) metadata from the given Motion Photo file. Automatically
   * parses metadata first to extract the target presentation timestamp.
   *
   * @param filePath Path to the motion photo file.
   * @return JSON string containing AGTM metadata, or empty string on error/not found.
   */
  static native String extractAgtm(String filePath);

  /**
   * Extracts AGTM (SMPTE ST 2094-50) metadata from an in-memory Motion Photo byte buffer.
   * Automatically parses metadata first to extract the target presentation timestamp.
   *
   * @param data The in-memory Motion Photo byte array.
   * @return JSON string containing AGTM metadata, or empty string on error/not found.
   */
  static native String extractAgtmFromMemory(byte[] data);

  /**
   * Extracts AGTM (SMPTE ST 2094-50) metadata from the given file descriptor. Automatically parses
   * metadata first to extract the target presentation timestamp.
   *
   * @param fd File descriptor of the video or motion photo file.
   * @return JSON string containing AGTM metadata, or empty string on error/not found.
   */
  static native String extractAgtmFd(int fd);

  static native String extractAgtmFd(int fd, long offset, long length);

  /**
   * Extracts AGTM (SMPTE ST 2094-50) metadata for the video frame active at timestampUs.
   *
   * @param filePath Path to the video or motion photo file.
   * @param timestampUs Target presentation timestamp in microseconds.
   * @return JSON string containing AGTM metadata, or empty string on error/not found.
   */
  static native String extractAgtmAtTimestamp(String filePath, long timestampUs);

  /**
   * Extracts raw metadata from inputFilePath and writes to outputMetadataFilePath.
   *
   * @param inputFilePath Path to input image file.
   * @param outputMetadataFilePath Output path for extracted metadata.
   * @return true on success, false on failure.
   */
  static native boolean extractMetadata(String inputFilePath, String outputMetadataFilePath);
}
