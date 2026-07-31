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
import com.google.libmotionphoto.MotionPhotoExtractorService;
import com.google.libmotionphoto.IMotionPhotoExtractorService;
import java.io.File;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Unit tests for MotionPhotoExtractorService and its sandboxed AIDL IPC interface. */
@RunWith(AndroidJUnit4.class)
public class MotionPhotoExtractorServiceTest {

  private IMotionPhotoExtractorService service;
  private String motionPhotoPath;

  @Before
  public void setUp() {
    MotionPhotoExtractorService serviceInstance = new MotionPhotoExtractorService();
    IBinder binder = serviceInstance.onBind(new Intent());
    service = IMotionPhotoExtractorService.Stub.asInterface(binder);

    motionPhotoPath = TestUtil.getTestDataPath("motion_photo.MP.jpg");
  }

  @Test
  public void extractMotionPhoto_extractsComponentsSuccessfully() throws Exception {
    File outPhoto = File.createTempFile("service_ext_photo_", ".jpg");
    File outVideo = File.createTempFile("service_ext_video_", ".mp4");
    File outMetadata = File.createTempFile("service_ext_meta_", ".xml");
    outPhoto.deleteOnExit();
    outVideo.deleteOnExit();
    outMetadata.deleteOnExit();

    int extractResult =
        service.extractMotionPhoto(
            motionPhotoPath,
            outPhoto.getAbsolutePath(),
            outVideo.getAbsolutePath(),
            outMetadata.getAbsolutePath());

    assertThat(extractResult).isEqualTo(0);
    assertThat(outPhoto.exists()).isTrue();
    assertThat(outPhoto.length()).isGreaterThan(0L);
    assertThat(outVideo.exists()).isTrue();
    assertThat(outVideo.length()).isGreaterThan(0L);
    assertThat(outMetadata.exists()).isTrue();
    assertThat(outMetadata.length()).isGreaterThan(0L);
  }
}
