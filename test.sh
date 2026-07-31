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

# test.sh - runs the motion photo tests

# Is the test binary built?
if [ ! -f "build/bin/motion_photo_tests" ]; then
  echo "Test binary not found. Please run ./build.sh first."
  exit 1
fi

# Run the tests
# Handle Anaconda libstdc++ conflict on Linux
if [ -d "$HOME/anaconda3" ] && [ "$(uname)" = "Linux" ]; then
  # Try to find system libstdc++.so.6
  SYSTEM_LIBSTDCXX="/usr/lib/x86_64-linux-gnu/libstdc++.so.6"
  if [ -f "$SYSTEM_LIBSTDCXX" ]; then
    echo "Detecting Anaconda on Linux. Preloading system libstdc++ to avoid conflicts."
    LD_PRELOAD="$SYSTEM_LIBSTDCXX" ./build/bin/motion_photo_tests "$@"
  else
    ./build/bin/motion_photo_tests "$@"
  fi
else
  ./build/bin/motion_photo_tests "$@"
fi
