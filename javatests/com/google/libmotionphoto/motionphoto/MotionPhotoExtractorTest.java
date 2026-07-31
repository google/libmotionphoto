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
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Unit tests for MotionPhotoExtractor public Java APIs. */
@RunWith(AndroidJUnit4.class)
public class MotionPhotoExtractorTest {

  private String motionPhotoPath;

  @Before
  public void setUp() {
    motionPhotoPath = TestUtil.getTestDataPath("motion_photo.MP.jpg");
  }

  @Test
  public void extract_validMotionPhoto_extractsComponentsSuccessfully() throws Exception {
    File outPhoto = File.createTempFile("extracted_photo_", ".jpg");
    File outVideo = File.createTempFile("extracted_video_", ".mp4");
    File outMetadata = File.createTempFile("extracted_meta_", ".xml");
    outPhoto.deleteOnExit();
    outVideo.deleteOnExit();
    outMetadata.deleteOnExit();

    MotionPhotoExtractor.Params params = new MotionPhotoExtractor.Params();
    params.motionPhotoFile = motionPhotoPath;
    params.primaryImageOutput = outPhoto.getAbsolutePath();
    params.videoOutput = outVideo.getAbsolutePath();
    params.metadataOutput = outMetadata.getAbsolutePath();

    StringBuilder logs = new StringBuilder();
    int result = MotionPhotoExtractor.extract(params, logs::append);
    assertThat(result).isEqualTo(0);

    assertThat(outPhoto.exists()).isTrue();
    assertThat(outPhoto.length()).isGreaterThan(0L);

    assertThat(outVideo.exists()).isTrue();
    assertThat(outVideo.length()).isGreaterThan(0L);

    assertThat(outMetadata.exists()).isTrue();
    assertThat(outMetadata.length()).isGreaterThan(0L);
  }

  @Test
  public void extract_invalidFile_returnsNonZero() throws Exception {
    MotionPhotoExtractor.Params params = new MotionPhotoExtractor.Params();
    params.motionPhotoFile = "non_existent_file.jpg";
    params.primaryImageOutput = "/tmp/non_existent_out.jpg";

    int result = MotionPhotoExtractor.extract(params, msg -> {});
    assertThat(result).isNotEqualTo(0);
  }
}
