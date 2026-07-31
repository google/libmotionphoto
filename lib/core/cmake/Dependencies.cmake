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

include(FetchContent)

# ---------------------------------------------------------------------------
# Global Dependency Configuration Options
# ---------------------------------------------------------------------------
option(LIBMOTIONPHOTO_USE_SYSTEM_DEPS
    "Attempt to find and use system-installed third-party libraries first" ON)
option(LIBMOTIONPHOTO_DOWNLOAD_DEPS
    "Automatically download dependencies via FetchContent if not found on system" ON)

option(USE_SYSTEM_LIBHEIF
    "Use system-installed libheif library if available" ${LIBMOTIONPHOTO_USE_SYSTEM_DEPS})
option(USE_SYSTEM_MP4V2
    "Use system-installed mp4v2 library if available" ${LIBMOTIONPHOTO_USE_SYSTEM_DEPS})
option(USE_SYSTEM_MODP_B64
    "Use system-installed modp_b64 library if available" ${LIBMOTIONPHOTO_USE_SYSTEM_DEPS})

# ---------------------------------------------------------------------------
# 1. libheif Dependency Resolution
# ---------------------------------------------------------------------------
set(LIBHEIF_FOUND FALSE)
set(LIBHEIF_INCLUDE_DIRS "")

if(USE_SYSTEM_LIBHEIF)
  # 1. Try CMake config package
  find_package(libheif QUIET CONFIG)
  if(libheif_FOUND)
    message(STATUS "Found system libheif via CMake config: ${libheif_DIR}")
    set(LIBHEIF_FOUND TRUE)
    if(NOT TARGET heif)
      if(TARGET libheif::heif)
        add_library(heif ALIAS libheif::heif)
      elseif(TARGET libheif::libheif)
        add_library(heif ALIAS libheif::libheif)
      endif()
    endif()
  endif()

  # 2. Try PkgConfig
  if(NOT LIBHEIF_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(PC_LIBHEIF QUIET IMPORTED_TARGET libheif)
      if(PC_LIBHEIF_FOUND)
        message(STATUS "Found system libheif via pkg-config (${PC_LIBHEIF_VERSION})")
        set(LIBHEIF_FOUND TRUE)
        if(NOT TARGET heif)
          add_library(heif INTERFACE)
          target_link_libraries(heif INTERFACE PkgConfig::PC_LIBHEIF)
        endif()
        set(LIBHEIF_INCLUDE_DIRS ${PC_LIBHEIF_INCLUDE_DIRS})
      endif()
    endif()
  endif()

  # 3. Try standard find_path and find_library
  if(NOT LIBHEIF_FOUND)
    find_path(LIBHEIF_SYSTEM_INCLUDE_DIR NAMES libheif/heif.h)
    find_library(LIBHEIF_SYSTEM_LIBRARY NAMES heif libheif)
    if(LIBHEIF_SYSTEM_INCLUDE_DIR AND LIBHEIF_SYSTEM_LIBRARY)
      message(STATUS "Found system libheif library: ${LIBHEIF_SYSTEM_LIBRARY}")
      set(LIBHEIF_FOUND TRUE)
      if(NOT TARGET heif)
        add_library(heif UNKNOWN IMPORTED)
        set_target_properties(heif PROPERTIES
          IMPORTED_LOCATION "${LIBHEIF_SYSTEM_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${LIBHEIF_SYSTEM_INCLUDE_DIR}"
        )
      endif()
      set(LIBHEIF_INCLUDE_DIRS ${LIBHEIF_SYSTEM_INCLUDE_DIR})
    endif()
  endif()
endif()

if(NOT LIBHEIF_FOUND)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libheif/CMakeLists.txt")
    message(STATUS "Using local vendored libheif in third_party/libheif")
    set(WITH_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "Build testing" FORCE)
    set(WITH_GDK_PIXBUF OFF CACHE BOOL "Build gdk-pixbuf plugin" FORCE)
    set(BUILD_DOCUMENTATION OFF CACHE BOOL "Build doxygen documentation" FORCE)
    set(WITH_FUZZERS OFF CACHE BOOL "Build fuzzers" FORCE)
    add_subdirectory(third_party/libheif)
    set(LIBHEIF_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libheif/libheif/api
        ${CMAKE_CURRENT_BINARY_DIR}/third_party/libheif
        ${CMAKE_CURRENT_BINARY_DIR}/third_party/libheif/libheif
    )
  elseif(LIBMOTIONPHOTO_DOWNLOAD_DEPS)
    message(STATUS "Fetching libheif from upstream (https://github.com/strukturag/libheif.git)...")
    set(WITH_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "Build testing" FORCE)
    set(WITH_GDK_PIXBUF OFF CACHE BOOL "Build gdk-pixbuf plugin" FORCE)
    set(BUILD_DOCUMENTATION OFF CACHE BOOL "Build doxygen documentation" FORCE)
    set(WITH_FUZZERS OFF CACHE BOOL "Build fuzzers" FORCE)
    FetchContent_Declare(
      libheif
      GIT_REPOSITORY https://github.com/strukturag/libheif.git
      GIT_TAG v1.17.6
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(libheif)
    set(LIBHEIF_FOUND TRUE)
    set(LIBHEIF_INCLUDE_DIRS
        ${libheif_SOURCE_DIR}
        ${libheif_SOURCE_DIR}/libheif
        ${libheif_SOURCE_DIR}/libheif/api
        ${libheif_BINARY_DIR}
        ${libheif_BINARY_DIR}/libheif
    )
  else()
    message(FATAL_ERROR "libheif dependency could not be resolved. Please install libheif-dev or enable LIBMOTIONPHOTO_DOWNLOAD_DEPS.")
  endif()
endif()

# ---------------------------------------------------------------------------
# 2. mp4v2 Dependency Resolution
# ---------------------------------------------------------------------------
set(MP4V2_FOUND FALSE)
set(MP4V2_INCLUDE_DIRS "")

if(USE_SYSTEM_MP4V2)
  find_package(mp4v2 QUIET CONFIG)
  if(mp4v2_FOUND)
    message(STATUS "Found system mp4v2 via CMake config")
    set(MP4V2_FOUND TRUE)
  else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(PC_MP4V2 QUIET libmp4v2 mp4v2)
      if(PC_MP4V2_FOUND)
        message(STATUS "Found system mp4v2 via pkg-config")
        set(MP4V2_FOUND TRUE)
        if(NOT TARGET mp4v2)
          add_library(mp4v2 INTERFACE)
          target_link_libraries(mp4v2 INTERFACE PkgConfig::PC_MP4V2)
        endif()
        set(MP4V2_INCLUDE_DIRS ${PC_MP4V2_INCLUDE_DIRS})
      endif()
    endif()
  endif()

  if(NOT MP4V2_FOUND)
    find_path(MP4V2_SYSTEM_INCLUDE_DIR NAMES mp4v2/mp4v2.h)
    find_library(MP4V2_SYSTEM_LIBRARY NAMES mp4v2 libmp4v2)
    if(MP4V2_SYSTEM_INCLUDE_DIR AND MP4V2_SYSTEM_LIBRARY)
      message(STATUS "Found system mp4v2 library: ${MP4V2_SYSTEM_LIBRARY}")
      set(MP4V2_FOUND TRUE)
      if(NOT TARGET mp4v2)
        add_library(mp4v2 UNKNOWN IMPORTED)
        set_target_properties(mp4v2 PROPERTIES
          IMPORTED_LOCATION "${MP4V2_SYSTEM_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${MP4V2_SYSTEM_INCLUDE_DIR}"
        )
      endif()
      set(MP4V2_INCLUDE_DIRS ${MP4V2_SYSTEM_INCLUDE_DIR})
    endif()
  endif()
endif()

if(NOT MP4V2_FOUND)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mp4v2/CMakeLists.txt")
    message(STATUS "Using local vendored mp4v2 in third_party/mp4v2")
    add_subdirectory(third_party/mp4v2)
    set(MP4V2_FOUND TRUE)
    set(MP4V2_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mp4v2")
    set(MP4V2_INCLUDE_DIRS
        ${MP4V2_SOURCE_DIR}
        ${MP4V2_SOURCE_DIR}/include
        ${MP4V2_SOURCE_DIR}/libutil
    )
  elseif(LIBMOTIONPHOTO_DOWNLOAD_DEPS)
    message(STATUS "Fetching mp4v2 from upstream (https://github.com/techsmith/mp4v2.git)...")
    FetchContent_Declare(
      mp4v2
      GIT_REPOSITORY https://github.com/techsmith/mp4v2.git
      GIT_TAG Release-ThirdParty-MP4v2-4.1.4
      GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(mp4v2)
    if(NOT mp4v2_POPULATED)
      FetchContent_Populate(mp4v2)
      if(NOT EXISTS "${mp4v2_SOURCE_DIR}/libplatform/config.h")
        file(WRITE "${mp4v2_SOURCE_DIR}/libplatform/config.h"
          "/* Generated config.h for mp4v2 */\n#ifndef MP4V2_CONFIG_H\n#define MP4V2_CONFIG_H 1\n#define HAVE_INTTYPES_H 1\n#define HAVE_STDINT_H 1\n#define HAVE_SYS_TYPES_H 1\n#endif\n"
        )
      endif()
      find_package(Python3 QUIET COMPONENTS Interpreter)
      if(Python3_Interpreter_FOUND)
        set(MP4V2_PATCH_SCRIPT [=[
import sys
src_dir = sys.argv[1]
# 1. include/mp4v2/file.h
with open(src_dir + '/include/mp4v2/file.h', 'r+') as f:
    content = f.read()
    if 'MP4ReadFromOffset' not in content:
        content = content.replace('#endif /* MP4V2_FILE_H */', 'MP4V2_EXPORT\nMP4FileHandle MP4ReadFromOffset(\n    const char* fileName,\n    int64_t seekOffset );\n\n#endif /* MP4V2_FILE_H */')
        f.seek(0); f.write(content); f.truncate()
# 2. src/mp4file.h
with open(src_dir + '/src/mp4file.h', 'r+') as f:
    content = f.read()
    if 'SetInitialSeekOffset' not in content:
        if 'void Close(uint32_t flags = 0);' in content:
            content = content.replace('void Close(uint32_t flags = 0);', 'void Close(uint32_t flags = 0);\n    void SetInitialSeekOffset(int64_t seekOffset);\n    int64_t m_initialSeekOffset;\n')
        else:
            content = content.replace('void Close();', 'void Close();\n    void SetInitialSeekOffset(int64_t seekOffset);\n    int64_t m_initialSeekOffset;\n')
        f.seek(0); f.write(content); f.truncate()
# 3. src/mp4file.cpp
with open(src_dir + '/src/mp4file.cpp', 'r+') as f:
    content = f.read()
    if 'SetInitialSeekOffset' not in content:
        content = content.replace('m_useIsma = false;', 'm_useIsma = false;\n    m_initialSeekOffset = 0;')
        content += '\nvoid mp4v2::impl::MP4File::SetInitialSeekOffset(int64_t seekOffset) {\n    m_initialSeekOffset = seekOffset;\n}\n'
        f.seek(0); f.write(content); f.truncate()
# 4. src/mp4file_io.cpp
with open(src_dir + '/src/mp4file_io.cpp', 'r+') as f:
    content = f.read()
    if 'm_initialSeekOffset' not in content:
        content = content.replace('return file->position;', 'return file->position - m_initialSeekOffset;')
        content = content.replace('if( file->seek( pos ))', 'if( file->seek( pos + m_initialSeekOffset ))')
        content = content.replace('return file->size;', 'return file->size - m_initialSeekOffset;')
        f.seek(0); f.write(content); f.truncate()
# 5. src/mp4.cpp
with open(src_dir + '/src/mp4.cpp', 'r+') as f:
    content = f.read()
    if 'MP4ReadFromOffset' not in content:
        content += '\nMP4FileHandle MP4ReadFromOffset(const char* fileName, int64_t seekOffset) {\n    if (!fileName) return MP4_INVALID_FILE_HANDLE;\n    MP4File* pFile = ConstructMP4File();\n    if (!pFile) return MP4_INVALID_FILE_HANDLE;\n    try {\n        ASSERT(pFile);\n        pFile->SetInitialSeekOffset(seekOffset);\n        pFile->Read(fileName, NULL);\n        return (MP4FileHandle)pFile;\n    } catch (Exception* x) {\n        mp4v2::impl::log.errorf(*x);\n        delete x;\n    } catch (...) {\n        mp4v2::impl::log.errorf("%s: \\"%s\\": failed", __FUNCTION__, fileName);\n    }\n    if (pFile) delete pFile;\n    return MP4_INVALID_FILE_HANDLE;\n}\n'
    content = content.replace('return MP4_INVALID_TRACK_ID;', 'return NULL;')
    f.seek(0); f.write(content); f.truncate()
# 6. src/mp4track.cpp
with open(src_dir + '/src/mp4track.cpp', 'r+') as f:
    content = f.read()
    if 'throw new Exception( "invalid stsd entry"' in content:
        content = content.replace('throw new Exception( "invalid stsd entry", __FILE__, __LINE__, __FUNCTION__ );', 'return nullptr; // throw new Exception( "invalid stsd entry" );')
        f.seek(0); f.write(content); f.truncate()
# 7. src/rtphint.cpp
with open(src_dir + '/src/rtphint.cpp', 'r+') as f:
    content = f.read()
    content = content.replace("if (pSlash != '\\0')", "if (*pSlash != '\\0')")
    f.seek(0); f.write(content); f.truncate()
]=])
        execute_process(
          COMMAND ${Python3_EXECUTABLE} -c "${MP4V2_PATCH_SCRIPT}" "${mp4v2_SOURCE_DIR}"
        )
      endif()
      file(GLOB MP4V2_SRC_SOURCES
        "${mp4v2_SOURCE_DIR}/src/*.cpp"
        "${mp4v2_SOURCE_DIR}/src/bmff/*.cpp"
        "${mp4v2_SOURCE_DIR}/src/itmf/*.cpp"
        "${mp4v2_SOURCE_DIR}/src/qtff/*.cpp"
        "${mp4v2_SOURCE_DIR}/libutil/*.cpp"
      )
      set(MP4V2_LIBPLATFORM_SOURCES
        ${mp4v2_SOURCE_DIR}/libplatform/io/File.cpp
        ${mp4v2_SOURCE_DIR}/libplatform/io/FileSystem.cpp
        ${mp4v2_SOURCE_DIR}/libplatform/io/File_posix.cpp
        ${mp4v2_SOURCE_DIR}/libplatform/io/FileSystem_posix.cpp
        ${mp4v2_SOURCE_DIR}/libplatform/number/random_posix.cpp
        ${mp4v2_SOURCE_DIR}/libplatform/process/process_posix.cpp
        ${mp4v2_SOURCE_DIR}/libplatform/prog/option.cpp
        ${mp4v2_SOURCE_DIR}/libplatform/sys/error.cpp
        ${mp4v2_SOURCE_DIR}/libplatform/time/time.cpp
        ${mp4v2_SOURCE_DIR}/libplatform/time/time_posix.cpp
      )
      add_library(mp4v2 STATIC ${MP4V2_SRC_SOURCES} ${MP4V2_LIBPLATFORM_SOURCES})
      target_compile_options(mp4v2 PRIVATE -DHAVE_CONFIG_H -fpermissive -fexceptions -Wno-narrowing -Wno-format -Wno-format-security -Wno-invalid-source-encoding)
      target_include_directories(mp4v2 PUBLIC
        ${mp4v2_SOURCE_DIR}
        ${mp4v2_SOURCE_DIR}/include
        ${mp4v2_SOURCE_DIR}/libutil
        ${mp4v2_SOURCE_DIR}/libplatform
      )
    endif()
    set(MP4V2_SOURCE_DIR "${mp4v2_SOURCE_DIR}")
    set(MP4V2_INCLUDE_DIRS
        ${MP4V2_SOURCE_DIR}
        ${MP4V2_SOURCE_DIR}/include
        ${MP4V2_SOURCE_DIR}/libutil
        ${MP4V2_SOURCE_DIR}/libplatform
    )
  else()
    message(FATAL_ERROR "mp4v2 dependency could not be resolved. Please install mp4v2 or enable LIBMOTIONPHOTO_DOWNLOAD_DEPS.")
  endif()
endif()

# ---------------------------------------------------------------------------
# 3. modp_b64 Dependency Resolution
# ---------------------------------------------------------------------------
set(MODP_B64_FOUND FALSE)
set(MODP_B64_INCLUDE_DIRS "")

if(USE_SYSTEM_MODP_B64)
  find_path(MODP_B64_SYSTEM_INCLUDE_DIR NAMES modp_b64.h)
  find_library(MODP_B64_SYSTEM_LIBRARY NAMES modp_b64 modp_b64w)
  if(MODP_B64_SYSTEM_INCLUDE_DIR AND MODP_B64_SYSTEM_LIBRARY)
    message(STATUS "Found system modp_b64: ${MODP_B64_SYSTEM_LIBRARY}")
    set(MODP_B64_FOUND TRUE)
    if(NOT TARGET modp_b64)
      add_library(modp_b64 UNKNOWN IMPORTED)
      set_target_properties(modp_b64 PROPERTIES
        IMPORTED_LOCATION "${MODP_B64_SYSTEM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MODP_B64_SYSTEM_INCLUDE_DIR}"
      )
    endif()
    set(MODP_B64_INCLUDE_DIRS ${MODP_B64_SYSTEM_INCLUDE_DIR})
  endif()
endif()

if(NOT MODP_B64_FOUND)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/modp_b64/CMakeLists.txt")
    message(STATUS "Using local vendored modp_b64 in third_party/modp_b64")
    add_subdirectory(third_party/modp_b64)
    set(MODP_B64_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/modp_b64")
    set(MODP_B64_INCLUDE_DIRS ${MODP_B64_SOURCE_DIR}/src)
  elseif(LIBMOTIONPHOTO_DOWNLOAD_DEPS)
    message(STATUS "Fetching modp_b64 from upstream (https://github.com/client9/stringencoders.git)...")
    FetchContent_Declare(
      modp_b64
      GIT_REPOSITORY https://github.com/client9/stringencoders.git
      GIT_TAG master
      GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(modp_b64)
    if(NOT modp_b64_POPULATED)
      FetchContent_Populate(modp_b64)
      if(NOT EXISTS "${modp_b64_SOURCE_DIR}/src/config.h")
        file(WRITE "${modp_b64_SOURCE_DIR}/src/config.h"
          "/* Generated config.h for modp_b64 */\n#ifndef MODP_B64_CONFIG_H\n#define MODP_B64_CONFIG_H 1\n#include <stdint.h>\n#include <stddef.h>\n#endif\n"
        )
      endif()
      if(NOT EXISTS "${modp_b64_SOURCE_DIR}/src/modp_b64_data.h")
        set(MODP_GEN_EXE "${CMAKE_CURRENT_BINARY_DIR}/modp_b64_gen")
        execute_process(
          COMMAND ${CMAKE_C_COMPILER} -o "${MODP_GEN_EXE}" "${modp_b64_SOURCE_DIR}/src/modp_b64_gen.c" "${modp_b64_SOURCE_DIR}/src/arraytoc.c"
        )
        if(EXISTS "${MODP_GEN_EXE}")
          execute_process(
            COMMAND "${MODP_GEN_EXE}"
            OUTPUT_FILE "${modp_b64_SOURCE_DIR}/src/modp_b64_data.h"
          )
        endif()
      endif()
      file(GLOB MODP_B64_SOURCES "${modp_b64_SOURCE_DIR}/src/modp_b64.c")
      add_library(modp_b64 STATIC ${MODP_B64_SOURCES})
      target_include_directories(modp_b64 PUBLIC ${modp_b64_SOURCE_DIR}/src)
    endif()
    set(MODP_B64_SOURCE_DIR "${modp_b64_SOURCE_DIR}")
    set(MODP_B64_INCLUDE_DIRS ${MODP_B64_SOURCE_DIR}/src)
  else()
    message(FATAL_ERROR "modp_b64 dependency could not be resolved. Please enable LIBMOTIONPHOTO_DOWNLOAD_DEPS.")
  endif()
endif()
