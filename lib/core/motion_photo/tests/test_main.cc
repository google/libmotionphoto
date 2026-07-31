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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

#include "motion_photo/tests/test_framework.h" // NOLINT

static void ChangeDirOrDie(const char* path) {
  if (chdir(path) != 0) {
    perror("chdir");
    exit(1);
  }
}

int main() {
  const char* test_srcdir = getenv("TEST_SRCDIR");
  const char* test_workspace = getenv("TEST_WORKSPACE");
#ifdef BAZEL_TEST_DIR
  if (test_srcdir && test_workspace) {
    std::string path =
        std::string(test_srcdir) + "/" + test_workspace + "/" + BAZEL_TEST_DIR;
    ChangeDirOrDie(path.c_str());
  } else {
    ChangeDirOrDie(BAZEL_TEST_DIR);
  }
#else
  if (test_srcdir && test_workspace) {
    std::string path = std::string(test_srcdir) + "/" + test_workspace;
    ChangeDirOrDie(path.c_str());
  }
#endif
  return ::libmotionphoto::test::TestRegistry::GetInstance().RunAllTests();
}
