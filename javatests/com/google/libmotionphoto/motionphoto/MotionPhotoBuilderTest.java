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
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Unit tests for MotionPhotoBuilder public Java APIs. */
@RunWith(AndroidJUnit4.class)
public class MotionPhotoBuilderTest {

  private String photoOnlyPath;
  private String videoOnlyPath;

  @Before
  public void setUp() {
    photoOnlyPath = TestUtil.getTestDataPath("motion_photo_photo_only.jpg");
    videoOnlyPath = TestUtil.getTestDataPath("motion_photo_video_only.mov");
  }

  @Test
  public void build_validParams_createsMotionPhoto() throws Exception {
    File tempOutput = File.createTempFile("builder_test_out_", ".jpg");
    tempOutput.deleteOnExit();

    MotionPhotoBuilder.Params params = new MotionPhotoBuilder.Params();
    params.primaryImage = photoOnlyPath;
    params.primaryVideo = videoOnlyPath;
    params.outputImage = tempOutput.getAbsolutePath();

    StringBuilder logs = new StringBuilder();
    int result = MotionPhotoBuilder.build(params, logs::append);
    assertThat(result).isEqualTo(0);
    assertThat(tempOutput.exists()).isTrue();
    assertThat(tempOutput.length()).isGreaterThan(0L);

    // Verify container with checker
    int checkResult = MotionPhotoChecker.check(tempOutput.getAbsolutePath(), msg -> {});
    assertThat(checkResult).isEqualTo(0);
  }

  @Test
  public void buildFromMemory_validBytes_createsMotionPhoto() throws Exception {
    File tempOutput = File.createTempFile("builder_mem_test_out_", ".jpg");
    tempOutput.deleteOnExit();

    byte[] imageBytes = Files.readAllBytes(Path.of(photoOnlyPath));
    byte[] videoBytes = Files.readAllBytes(Path.of(videoOnlyPath));

    int result =
        MotionPhotoBuilder.buildFromMemory(
            imageBytes, videoBytes, null, tempOutput.getAbsolutePath(), msg -> {});
    assertThat(result).isEqualTo(0);
    assertThat(tempOutput.exists()).isTrue();
    assertThat(tempOutput.length()).isGreaterThan(0L);

    // Verify container with checker
    int checkResult = MotionPhotoChecker.check(tempOutput.getAbsolutePath(), msg -> {});
    assertThat(checkResult).isEqualTo(0);
  }

  @Test
  public void build_invalidParams_returnsNonZero() throws Exception {
    MotionPhotoBuilder.Params params = new MotionPhotoBuilder.Params();
    params.primaryImage = "non_existent_image.jpg";
    params.primaryVideo = "non_existent_video.mp4";
    params.outputImage = "non_existent_out.jpg";

    int result = MotionPhotoBuilder.build(params, msg -> {});
    assertThat(result).isNotEqualTo(0);
  }
}
