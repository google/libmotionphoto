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

/**
 * Public Java API for the Motion Photo Metadata Engine.
 *
 * <p>Provides methods to parse metadata blocks, check for Motion Photo status, get details JSON,
 * and extract AGTM (SMPTE ST 2094-50) dynamic metadata at specific presentation timestamps.
 */
public class MetadataEngine {

  private MetadataEngine() {}

  /**
   * Parses metadata from the given image file path.
   *
   * @param filePath The path to the image file.
   * @return Serialized MetadataCollection proto bytes, or null on error.
   */
  public static byte[] parseMetadata(String filePath) {
    return MetadataEngineJni.parseMetadata(filePath);
  }

  /**
   * Parses metadata from the given file descriptor.
   *
   * @param fd The file descriptor of the image file.
   * @return Serialized MetadataCollection proto bytes, or null on error.
   */
  public static byte[] parseMetadataFd(int fd) {
    return MetadataEngineJni.parseMetadataFd(fd);
  }

  public static byte[] parseMetadataFd(int fd, long offset, long length) {
    return MetadataEngineJni.parseMetadataFd(fd, offset, length);
  }

  /**
   * Helper to check if the serialized MetadataCollection represents a motion photo.
   *
   * @param serializedCollection Serialized MetadataCollection proto bytes.
   * @return true if it is a motion photo, false otherwise.
   */
  public static boolean isMotionPhoto(byte[] serializedCollection) {
    return MetadataEngineJni.isMotionPhoto(serializedCollection);
  }

  /**
   * Helper to check if the serialized MetadataCollection represents a motion photo with options.
   *
   * @param serializedCollection Serialized MetadataCollection proto bytes.
   * @param disable3pPlugins Whether to disable 3P metadata plugins.
   * @param enabled3pPlugins List of enabled 3P metadata plugin names.
   * @return true if it is a motion photo, false otherwise.
   */
  @SuppressWarnings("AvoidObjectArrays")
  public static boolean isMotionPhoto(
      byte[] serializedCollection, boolean disable3pPlugins, String[] enabled3pPlugins) {
    return MetadataEngineJni.isMotionPhoto(
        serializedCollection, disable3pPlugins, enabled3pPlugins);
  }

  /**
   * Extracts AGTM (SMPTE ST 2094-50) metadata from the given Motion Photo file. Automatically
   * parses metadata first to extract the target presentation timestamp.
   *
   * @param filePath Path to the motion photo file.
   * @return JSON string containing AGTM metadata, or empty string on error/not found.
   */
  public static String extractAgtm(String filePath) {
    return MetadataEngineJni.extractAgtm(filePath);
  }

  /**
   * Extracts AGTM (SMPTE ST 2094-50) metadata from an in-memory Motion Photo byte buffer.
   * Automatically parses metadata first to extract the target presentation timestamp.
   *
   * @param data The in-memory Motion Photo byte array.
   * @return JSON string containing AGTM metadata, or empty string on error/not found.
   */
  public static String extractAgtm(byte[] data) {
    return MetadataEngineJni.extractAgtmFromMemory(data);
  }

  /**
   * Extracts AGTM (SMPTE ST 2094-50) metadata from the given file descriptor. Automatically parses
   * metadata first to extract the target presentation timestamp.
   *
   * @param fd File descriptor of the video or motion photo file.
   * @return JSON string containing AGTM metadata, or empty string on error/not found.
   */
  public static String extractAgtmFd(int fd) {
    return MetadataEngineJni.extractAgtmFd(fd);
  }

  /**
   * Extracts AGTM (SMPTE ST 2094-50) metadata from the given file descriptor slice. Automatically
   * parses metadata first to extract the target presentation timestamp.
   *
   * @param fd File descriptor of the video or motion photo file.
   * @param offset The starting byte offset.
   * @param length The slice length in bytes.
   * @return JSON string containing AGTM metadata, or empty string on error/not found.
   */
  public static String extractAgtmFd(int fd, long offset, long length) {
    return MetadataEngineJni.extractAgtmFd(fd, offset, length);
  }

  /**
   * Extracts AGTM (SMPTE ST 2094-50) metadata for the video frame active at timestampUs.
   *
   * @param filePath Path to the video or motion photo file.
   * @param timestampUs Target presentation timestamp in microseconds.
   * @return JSON string containing AGTM metadata, or empty string on error/not found.
   */
  @SuppressWarnings("ApiWithNumericTimeUnit")
  public static String extractAgtmAtTimestamp(String filePath, long timestampUs) {
    return MetadataEngineJni.extractAgtmAtTimestamp(filePath, timestampUs);
  }

  /**
   * Extracts raw metadata from inputFilePath and writes to outputMetadataFilePath.
   *
   * @param inputFilePath Path to input image file.
   * @param outputMetadataFilePath Output path for extracted metadata.
   * @return true on success, false on failure.
   */
  public static boolean extractMetadata(String inputFilePath, String outputMetadataFilePath) {
    return MetadataEngineJni.extractMetadata(inputFilePath, outputMetadataFilePath);
  }
}
