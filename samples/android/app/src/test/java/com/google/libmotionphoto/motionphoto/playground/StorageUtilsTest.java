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

package com.google.libmotionphoto.motionphoto.playground;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.net.Uri;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import java.io.File;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

/** Android Robolectric tests for StorageUtils HEIC/AVIF file extension preservation. */
@RunWith(AndroidJUnit4.class)
@Config(sdk = 34)
public class StorageUtilsTest {

  private static File resolveTestDataFile(String fileName) {
    String[] candidatePrefixes = {
      "../../../lib/core/motion_photo/testdata/",
      "../../lib/core/motion_photo/testdata/",
      "../lib/core/motion_photo/testdata/",
      "lib/core/motion_photo/testdata/"
    };
    for (String prefix : candidatePrefixes) {
      File candidate = new File(prefix + fileName);
      if (candidate.exists()) {
        return candidate.getAbsoluteFile();
      }
    }
    throw new IllegalStateException("Could not locate testdata file: " + fileName);
  }

  @Test
  public void copyUriToTempFile_preservesHeicExtensionInAndroidCacheDir() throws Exception {
    Context context = ApplicationProvider.getApplicationContext();
    File srcHeic = resolveTestDataFile("motion_photo_single_video_track.MP.heic");
    Uri fileUri = Uri.fromFile(srcHeic);

    String copiedPath =
        StorageUtils.copyUriToTempFile(context, fileUri, "picked_motion_photo", ".jpg");
    assertNotNull(copiedPath);
    File copied = new File(copiedPath);
    assertTrue(copied.exists());
    assertTrue(
        "Expected .heic suffix, got: " + copied.getName(), copied.getName().endsWith(".heic"));
    assertEquals(srcHeic.length(), copied.length());
    copied.delete();
  }
}
