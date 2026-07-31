#!/bin/bash
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

set -e

# Configure paths
if [ -n "${ANDROID_NDK_HOME}" ]; then
  NDK_PATH="${ANDROID_NDK_HOME}"
elif [ -n "${ANDROID_NDK_ROOT}" ]; then
  NDK_PATH="${ANDROID_NDK_ROOT}"
elif [ -n "${ANDROID_HOME}" ] && [ -d "${ANDROID_HOME}/ndk-bundle" ]; then
  NDK_PATH="${ANDROID_HOME}/ndk-bundle"
elif [ -n "${ANDROID_HOME}" ] && [ -d "${ANDROID_HOME}/ndk" ]; then
  NDK_PATH="$(find "${ANDROID_HOME}/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1)"
else
  echo "Error: ANDROID_NDK_HOME, ANDROID_NDK_ROOT, or ANDROID_HOME must be set." >&2
  exit 1
fi
ABI="arm64-v8a"
MIN_SDK="21"

WORKSPACE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
EXTERNAL_DIR="${WORKSPACE_DIR}/external"
INSTALL_DIR="${EXTERNAL_DIR}/install_${ABI}"
INSTALL_HOST_DIR="${EXTERNAL_DIR}/install_host"

echo "Workspace: ${WORKSPACE_DIR}"
echo "Install Dir: ${INSTALL_DIR}"
echo "Install Host Dir: ${INSTALL_HOST_DIR}"

# 0.a Build Abseil for Host
echo "Building Abseil for Host..."
mkdir -p "${EXTERNAL_DIR}/build_absl_host"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_HOST_DIR}" \
  -DABSL_PROPAGATE_CXX_STD=ON \
  -DCMAKE_CXX_STANDARD=17 \
  -H"${EXTERNAL_DIR}/protobuf/third_party/abseil-cpp" \
  -B"${EXTERNAL_DIR}/build_absl_host"

make -C "${EXTERNAL_DIR}/build_absl_host" -j8 install

# 0.b Build Protobuf for Host
echo "Building Protobuf for Host..."
mkdir -p "${EXTERNAL_DIR}/build_proto_host"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_HOST_DIR}" \
  -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_ABSL_PROVIDER=package \
  -DCMAKE_PREFIX_PATH="${INSTALL_HOST_DIR}" \
  -Dprotobuf_WITH_ZLIB=OFF \
  -H"${EXTERNAL_DIR}/protobuf" \
  -B"${EXTERNAL_DIR}/build_proto_host"

make -C "${EXTERNAL_DIR}/build_proto_host" -j8 install

# 1. Build Abseil for Target
echo "Building Abseil for Target..."
mkdir -p "${EXTERNAL_DIR}/build_absl"
cmake \
  -DCMAKE_TOOLCHAIN_FILE="${NDK_PATH}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="${ABI}" \
  -DANDROID_PLATFORM="android-${MIN_SDK}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
  -DABSL_PROPAGATE_CXX_STD=ON \
  -DCMAKE_CXX_STANDARD=17 \
  -H"${EXTERNAL_DIR}/protobuf/third_party/abseil-cpp" \
  -B"${EXTERNAL_DIR}/build_absl"

make -C "${EXTERNAL_DIR}/build_absl" -j8 install

# 2. Build Protobuf for Target
echo "Building Protobuf for Target..."
mkdir -p "${EXTERNAL_DIR}/build_proto"
cmake \
  -DCMAKE_TOOLCHAIN_FILE="${NDK_PATH}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="${ABI}" \
  -DANDROID_PLATFORM="android-${MIN_SDK}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
  -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
  -Dprotobuf_ABSL_PROVIDER=package \
  -DCMAKE_PREFIX_PATH="${INSTALL_DIR}" \
  -DCMAKE_FIND_ROOT_PATH="${INSTALL_DIR}" \
  -Dprotobuf_WITH_ZLIB=OFF \
  -H"${EXTERNAL_DIR}/protobuf" \
  -B"${EXTERNAL_DIR}/build_proto"

make -C "${EXTERNAL_DIR}/build_proto" -j8 install

echo "Dependencies built successfully in ${INSTALL_DIR}"
