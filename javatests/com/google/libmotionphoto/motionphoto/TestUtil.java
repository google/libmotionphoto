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

import com.google.devtools.build.runtime.RunfilesPaths;
import java.io.File;
import java.nio.file.Path;

/** Utilities for finding test data files and managing test resources. */
public final class TestUtil {
  private static final String TESTDATA_DIR =
      "experimental/users/dichenzhang/libmotionphoto/lib/core/motion_photo/testdata/";

  private TestUtil() {}

  /**
   * Resolves the absolute path to a test file in the testdata directory.
   *
   * @param filename Name of the test asset file.
   * @return Absolute path to the test file.
   * @throws IllegalArgumentException if the file cannot be located.
   */
  public static String getTestDataPath(String filename) {
    try {
      Path resolvedPath = RunfilesPaths.resolve(TESTDATA_DIR + filename);
      File f = resolvedPath.toFile();
      if (f.exists()) {
        return f.getAbsolutePath();
      }
    } catch (RuntimeException ignored) {
      // Fall through to fallback checks
    }

    String testSrcDir = System.getenv("TEST_SRCDIR");
    if (testSrcDir != null) {
      File f = new File(testSrcDir, "google3/" + TESTDATA_DIR + filename);
      if (f.exists()) {
        return f.getAbsolutePath();
      }
      f = new File(testSrcDir, TESTDATA_DIR + filename);
      if (f.exists()) {
        return f.getAbsolutePath();
      }
    }

    File f = new File(TESTDATA_DIR + filename);
    if (f.exists()) {
      return f.getAbsolutePath();
    }
    f = new File("lib/motion_photo/testdata/" + filename);
    if (f.exists()) {
      return f.getAbsolutePath();
    }

    throw new IllegalArgumentException("Could not locate test data file: " + filename);
  }
}
