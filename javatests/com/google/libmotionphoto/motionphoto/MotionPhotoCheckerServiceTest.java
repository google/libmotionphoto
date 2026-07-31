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

import android.content.Intent;
import android.os.IBinder;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import com.google.libmotionphoto.MotionPhotoCheckerService;
import com.google.libmotionphoto.IMotionPhotoCheckerService;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Unit tests for MotionPhotoCheckerService and its sandboxed AIDL IPC interface. */
@RunWith(AndroidJUnit4.class)
public class MotionPhotoCheckerServiceTest {

  private IMotionPhotoCheckerService service;
  private String motionPhotoPath;
  private String photoOnlyPath;

  @Before
  public void setUp() {
    MotionPhotoCheckerService serviceInstance = new MotionPhotoCheckerService();
    IBinder binder = serviceInstance.onBind(new Intent());
    service = IMotionPhotoCheckerService.Stub.asInterface(binder);

    motionPhotoPath = TestUtil.getTestDataPath("motion_photo.MP.jpg");
    photoOnlyPath = TestUtil.getTestDataPath("motion_photo_photo_only.jpg");
  }

  @Test
  public void checkMotionPhoto_validMotionPhoto_returnsZero() throws Exception {
    int result = service.checkMotionPhoto(motionPhotoPath);
    assertThat(result).isEqualTo(0);
  }

  @Test
  public void checkMotionPhoto_invalidFile_returnsNonZero() throws Exception {
    int result = service.checkMotionPhoto(photoOnlyPath);
    assertThat(result).isNotEqualTo(0);
  }
}
