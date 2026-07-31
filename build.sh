#!/bin/sh
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

# build.sh - builds the motion photo libraries and binaries

# Is cmake installed?
type cmake >/dev/null 2>&1 || { printf "The cmake program was not found.\nDownload from 'https://cmake.org/download/' and install\n"; exit 1; }

# Make the build directory if needed
if [ ! -d "build" ]; then
  mkdir build
fi

# Detect Anaconda and set prefix path
CMAKE_FLAGS=""
if [ -d "$HOME/anaconda3" ]; then
  CMAKE_FLAGS="-DCMAKE_PREFIX_PATH=$HOME/anaconda3"
fi

# Change to the build directory and run cmake and make
cd build
cmake $CMAKE_FLAGS "$@" ..
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
make -j"$JOBS"
