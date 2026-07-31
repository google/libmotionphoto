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

import static com.google.common.truth.Truth.assertThat;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Unit tests for MotionPhotoChecker public Java APIs. */
@RunWith(AndroidJUnit4.class)
public class MotionPhotoCheckerTest {

  private String motionPhotoPath;
  private String photoOnlyPath;

  @Before
  public void setUp() {
    motionPhotoPath = TestUtil.getTestDataPath("motion_photo.MP.jpg");
    photoOnlyPath = TestUtil.getTestDataPath("motion_photo_photo_only.jpg");
  }

  @Test
  public void check_validMotionPhoto_returnsZero() {
    StringBuilder logs = new StringBuilder();
    int result = MotionPhotoChecker.check(motionPhotoPath, logs::append);
    assertThat(result).isEqualTo(0);
    assertThat(logs.toString()).isNotEmpty();
  }

  @Test
  public void check_nonMotionPhoto_returnsNonZero() {
    StringBuilder logs = new StringBuilder();
    int result = MotionPhotoChecker.check(photoOnlyPath, logs::append);
    assertThat(result).isNotEqualTo(0);
  }
}
