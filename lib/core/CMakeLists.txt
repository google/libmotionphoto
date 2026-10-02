# Copyright 2026 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.14)

project (MotionPhoto)

SET(CMAKE_CXX_STANDARD 17)
include_directories(${CMAKE_SOURCE_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(Protobuf CONFIG QUIET)
if (NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()
include(cmake/Dependencies.cmake)

if (LIBMOTIONPHOTO_USE_SYSTEM_DEPS)
  find_package(absl CONFIG QUIET)
  if (NOT absl_FOUND)
    find_package(absl QUIET)
  endif()
endif()
if (NOT TARGET absl::strings)
  include(FetchContent)
  FetchContent_Declare(
    abseil_cpp
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20240116.2
    GIT_SHALLOW TRUE
  )
  set(ABSL_PROPAGATE_CXX_STANDARD ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(abseil_cpp)
endif()
if (NOT HOST_PROTOC_EXECUTABLE)
  find_program(PROTOC_BINARY NAMES protoc PATHS /usr/bin /usr/local/bin /opt/homebrew/bin "$ENV{HOME}/anaconda3/bin")
  if (PROTOC_BINARY)
    set(Protobuf_PROTOC_EXECUTABLE "${PROTOC_BINARY}" CACHE FILEPATH "The protoc compiler" FORCE)
    set(HOST_PROTOC_EXECUTABLE "${PROTOC_BINARY}")
  endif()
else()
  set(PROTOC_BINARY "${HOST_PROTOC_EXECUTABLE}")
  set(Protobuf_PROTOC_EXECUTABLE "${HOST_PROTOC_EXECUTABLE}" CACHE FILEPATH "The protoc compiler" FORCE)
endif()

if (NOT TARGET protobuf::protoc AND PROTOC_BINARY)
  add_executable(protobuf::protoc IMPORTED)
  set_target_properties(protobuf::protoc PROPERTIES
    IMPORTED_LOCATION "${PROTOC_BINARY}"
  )
endif()

if (LIBHEIF_FOUND)
  add_compile_definitions(MOTION_PHOTO_HEIC_SUPPORT=1)
endif()

include_directories(. third_party/image_io/includes ${PROTOBUF_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

protobuf_generate(
    OUT_VAR PROTO_FILES
    LANGUAGE cpp
    PROTOC_EXE ${HOST_PROTOC_EXECUTABLE}
    APPEND_PATH
    IMPORT_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../samples/android/external/install_arm64-v8a/include
    PROTOS
        plugins/legacy_micro_video/legacy_micro_video_metadata.proto
        motion_photo/proto/metadata_collection.proto
        motion_photo/proto/motion_photo_metadata.proto
)
add_library(motion_photo_protos ${PROTO_FILES})
target_link_libraries(motion_photo_protos PUBLIC protobuf::libprotobuf-lite)
file(GLOB IMAGE_IO_BASE_SOURCES "third_party/image_io/src/base/*.cc")
add_library(image_io_base ${IMAGE_IO_BASE_SOURCES})

file(GLOB IMAGE_IO_ISO_SOURCES "third_party/image_io/src/iso/*.cc")
add_library(image_io_iso ${IMAGE_IO_ISO_SOURCES})
target_link_libraries(image_io_iso PUBLIC image_io_base)

file(GLOB IMAGE_IO_UTILS_SOURCES "third_party/image_io/src/utils/*.cc")
add_library(image_io_utils ${IMAGE_IO_UTILS_SOURCES})
target_link_libraries(image_io_utils PUBLIC image_io_base)

file(GLOB IMAGE_IO_XML_SOURCES "third_party/image_io/src/xml/*.cc")
add_library(image_io_xml ${IMAGE_IO_XML_SOURCES})
target_link_libraries(image_io_xml PUBLIC image_io_base)

file(GLOB IMAGE_IO_XMP_SOURCES "third_party/image_io/src/xmp/*.cc")
add_library(image_io_xmp ${IMAGE_IO_XMP_SOURCES})
target_link_libraries(image_io_xmp PUBLIC image_io_base image_io_xml)

file(GLOB IMAGE_IO_MPF_SOURCES "third_party/image_io/src/mpf/*.cc")
add_library(image_io_mpf ${IMAGE_IO_MPF_SOURCES})
target_link_libraries(image_io_mpf PUBLIC image_io_base image_io_xmp)

file(GLOB IMAGE_IO_BASE64_SOURCES "third_party/image_io/src/extras/base64*.cc")
add_library(image_io_base64 ${IMAGE_IO_BASE64_SOURCES})
target_include_directories(image_io_base64 PRIVATE ${MODP_B64_INCLUDE_DIRS})
target_link_libraries(image_io_base64 PUBLIC image_io_base PRIVATE modp_b64)

file(GLOB IMAGE_IO_JPEG_SOURCES "third_party/image_io/src/jpeg/*.cc")
add_library(image_io_jpeg ${IMAGE_IO_JPEG_SOURCES})
target_link_libraries(image_io_jpeg PUBLIC image_io_base image_io_base64)

file(GLOB MOTION_PHOTO_MP4_SOURCES "motion_photo/src/motion_photo_mp4/*.cc")
add_library(motion_photo_mp4 ${MOTION_PHOTO_MP4_SOURCES})
target_include_directories(motion_photo_mp4 PUBLIC . third_party/image_io/includes motion_photo/includes ${MP4V2_INCLUDE_DIRS})
target_link_libraries(motion_photo_mp4 PUBLIC image_io_base image_io_utils PRIVATE mp4v2)

include_directories(motion_photo/includes)
file(GLOB MOTION_PHOTO_SOURCES "motion_photo/src/motion_photo/*.cc")
list(FILTER MOTION_PHOTO_SOURCES EXCLUDE REGEX ".*libmotionphoto_api\\.cc$")
file(GLOB PLUGINS_SOURCES "plugins/*/*.cc")
list(FILTER PLUGINS_SOURCES EXCLUDE REGEX ".*_test\\.cc$")
add_library(motion_photo ${MOTION_PHOTO_SOURCES} ${PLUGINS_SOURCES})
target_link_libraries(motion_photo PUBLIC image_io_base image_io_iso image_io_xml image_io_xmp image_io_jpeg image_io_utils motion_photo_protos motion_photo_mp4 absl::status absl::span)

file(GLOB MOTION_PHOTO_JPEG_SOURCES "motion_photo/src/motion_photo_jpeg/*.cc")
add_library(motion_photo_jpeg ${MOTION_PHOTO_JPEG_SOURCES})
target_link_libraries(motion_photo_jpeg PUBLIC motion_photo image_io_base image_io_jpeg image_io_utils image_io_mpf absl::algorithm_container)

file(GLOB MOTION_PHOTO_HEIF_SOURCES "motion_photo/src/motion_photo_heif/*.cc")
add_library(motion_photo_heif ${MOTION_PHOTO_HEIF_SOURCES})
target_include_directories(motion_photo_heif PUBLIC . third_party/image_io/includes motion_photo/includes ${LIBHEIF_INCLUDE_DIRS})
target_link_libraries(motion_photo_heif PUBLIC motion_photo image_io_base image_io_iso image_io_utils PRIVATE heif)

file(GLOB MOTION_PHOTO_COMMON_SOURCES "motion_photo/src/motion_photo_checker_extractor_common/*.cc")
add_library(motion_photo_checker_extractor_common ${MOTION_PHOTO_COMMON_SOURCES})
target_include_directories(motion_photo_checker_extractor_common PUBLIC . motion_photo/src third_party/image_io/includes motion_photo/includes ${LIBHEIF_INCLUDE_DIRS} ${MP4V2_INCLUDE_DIRS})
target_link_libraries(motion_photo_checker_extractor_common PUBLIC motion_photo motion_photo_jpeg motion_photo_heif motion_photo_mp4 image_io_base image_io_utils)

# Builder Library
add_library(motion_photo_builder_lib motion_photo/src/motion_photo_builder/function.cc)
target_link_libraries(motion_photo_builder_lib PUBLIC motion_photo motion_photo_jpeg motion_photo_heif motion_photo_mp4 image_io_base image_io_jpeg image_io_utils image_io_xmp)

# Checker Library
add_library(motion_photo_checker_lib motion_photo/src/motion_photo_checker/function.cc)
target_link_libraries(motion_photo_checker_lib PUBLIC motion_photo_checker_extractor_common motion_photo motion_photo_jpeg motion_photo_heif motion_photo_mp4 image_io_base image_io_jpeg image_io_utils image_io_xmp)

# Extractor Library
add_library(motion_photo_extractor_lib motion_photo/src/motion_photo_extractor/function.cc)
target_link_libraries(motion_photo_extractor_lib PUBLIC motion_photo_checker_extractor_common motion_photo motion_photo_jpeg motion_photo_heif motion_photo_mp4 image_io_base image_io_jpeg image_io_utils image_io_xmp)

# Metadata Engine CLI Library
add_library(motion_photo_metadata_engine_lib motion_photo/src/motion_photo_metadata_engine/function.cc)
target_include_directories(motion_photo_metadata_engine_lib PUBLIC motion_photo/includes/motion_photo_metadata_engine)
target_link_libraries(motion_photo_metadata_engine_lib PUBLIC motion_photo_checker_extractor_common motion_photo image_io_base image_io_utils)

# Standalone Public API Library
add_library(motion_photo_api motion_photo/src/motion_photo/libmotionphoto_api.cc)
target_include_directories(motion_photo_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/motion_photo/includes/libmotionphoto>
        $<INSTALL_INTERFACE:include/libmotionphoto>
    PRIVATE
        .
        includes
        motion_photo/includes
        third_party/image_io/includes
        ${LIBHEIF_INCLUDE_DIRS}
        ${MP4V2_INCLUDE_DIRS}
)
target_link_libraries(motion_photo_api
    PRIVATE
        motion_photo_builder_lib
        motion_photo_checker_lib
        motion_photo_extractor_lib
        motion_photo_metadata_engine_lib
        motion_photo_checker_extractor_common
        motion_photo
        image_io_base
        image_io_utils
)

if (NOT ANDROID)
  add_executable(motion_photo_builder motion_photo/src/motion_photo_builder/main.cc)
  target_link_libraries(motion_photo_builder motion_photo_builder_lib)

  add_executable(motion_photo_checker motion_photo/src/motion_photo_checker/main.cc)
  target_link_libraries(motion_photo_checker motion_photo_checker_lib)

  add_executable(motion_photo_extractor motion_photo/src/motion_photo_extractor/main.cc)
  target_link_libraries(motion_photo_extractor motion_photo_extractor_lib)

  add_executable(motion_photo_metadata_engine motion_photo/src/motion_photo_metadata_engine/main.cc)
  target_link_libraries(motion_photo_metadata_engine motion_photo_metadata_engine_lib)

  # Tests
  add_executable(motion_photo_tests
    motion_photo/tests/test_main.cc
    plugins/legacy_micro_video/legacy_micro_video_provider_test.cc
    motion_photo/tests/motion_photo_builder_test.cc
    motion_photo/tests/motion_photo_checker_test.cc
    motion_photo/tests/motion_photo_extractor_test.cc
    motion_photo/tests/mp4_box_reader_test.cc
    motion_photo/tests/libmotionphoto_api_test.cc
  )
  target_include_directories(motion_photo_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. motion_photo/includes ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(motion_photo_tests
    PRIVATE
      motion_photo_api
      motion_photo_builder_lib
      motion_photo_checker_lib
      motion_photo_extractor_lib
      motion_photo_metadata_engine_lib
      motion_photo
      protobuf::libprotobuf-lite
      absl::strings
  )
endif()

# JNI Library
if (ANDROID)
  add_library(motion_photo_jni SHARED motion_photo/src/motion_photo_jni/motion_photo_jni.cc)
  target_link_libraries(motion_photo_jni PRIVATE
      motion_photo_builder_lib
      motion_photo_checker_lib
      motion_photo_extractor_lib
      motion_photo
      protobuf::libprotobuf-lite
      $<$<TARGET_EXISTS:absl::log_internal_check_op>:absl::log_internal_check_op>
  )
  target_link_options(motion_photo_jni PRIVATE "-Wl,-z,max-page-size=16384")
else()
  find_package(JNI REQUIRED)
  if (JNI_FOUND)
    add_library(motion_photo_jni SHARED motion_photo/src/motion_photo_jni/motion_photo_jni.cc)
    target_include_directories(motion_photo_jni PRIVATE ${JNI_INCLUDE_DIRS})
    target_link_libraries(motion_photo_jni PRIVATE
        motion_photo_builder_lib
        motion_photo_checker_lib
        motion_photo_extractor_lib
        motion_photo
        protobuf::libprotobuf-lite
        $<$<TARGET_EXISTS:absl::log_internal_check_op>:absl::log_internal_check_op>
        ${JNI_LIBRARIES}
    )
  endif()
endif()

# Install Rules: Only export and install the public API facade header and library
install(TARGETS motion_photo_api
    EXPORT libmotionphotoTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include/libmotionphoto
)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libmotionphoto_api.h
    DESTINATION include/libmotionphoto
)
