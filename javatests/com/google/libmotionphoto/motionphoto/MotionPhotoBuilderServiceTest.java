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
import com.google.libmotionphoto.MotionPhotoBuilderService;
import com.google.libmotionphoto.IMotionPhotoBuilderService;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Unit tests for MotionPhotoBuilderService and its sandboxed AIDL IPC interface. */
@RunWith(AndroidJUnit4.class)
public class MotionPhotoBuilderServiceTest {

  private IMotionPhotoBuilderService service;
  private String photoOnlyPath;
  private String videoOnlyPath;

  @Before
  public void setUp() {
    MotionPhotoBuilderService serviceInstance = new MotionPhotoBuilderService();
    IBinder binder = serviceInstance.onBind(new Intent());
    service = IMotionPhotoBuilderService.Stub.asInterface(binder);

    photoOnlyPath = TestUtil.getTestDataPath("motion_photo_photo_only.jpg");
    videoOnlyPath = TestUtil.getTestDataPath("motion_photo_video_only.mov");
  }

  @Test
  public void buildMotionPhoto_createsValidContainer() throws Exception {
    File tempOutput = File.createTempFile("service_build_out_", ".jpg");
    tempOutput.deleteOnExit();

    int buildResult =
        service.buildMotionPhoto(
            photoOnlyPath, videoOnlyPath, null, null, -1, tempOutput.getAbsolutePath());
    assertThat(buildResult).isEqualTo(0);
    assertThat(tempOutput.exists()).isTrue();
    assertThat(tempOutput.length()).isGreaterThan(0L);

    int checkResult = MotionPhotoChecker.check(tempOutput.getAbsolutePath(), msg -> {});
    assertThat(checkResult).isEqualTo(0);
  }

  @Test
  public void buildMotionPhotoFromMemory_createsValidContainer() throws Exception {
    File tempOutput = File.createTempFile("service_build_mem_out_", ".jpg");
    tempOutput.deleteOnExit();

    byte[] imageBytes = Files.readAllBytes(Path.of(photoOnlyPath));
    byte[] videoBytes = Files.readAllBytes(Path.of(videoOnlyPath));

    int buildResult =
        service.buildMotionPhotoFromMemory(
            imageBytes, videoBytes, null, tempOutput.getAbsolutePath());
    assertThat(buildResult).isEqualTo(0);
    assertThat(tempOutput.exists()).isTrue();
    assertThat(tempOutput.length()).isGreaterThan(0L);

    int checkResult = MotionPhotoChecker.check(tempOutput.getAbsolutePath(), msg -> {});
    assertThat(checkResult).isEqualTo(0);
  }
}
