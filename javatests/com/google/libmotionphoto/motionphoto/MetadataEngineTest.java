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

import android.os.ParcelFileDescriptor;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Unit tests for MetadataEngine public Java APIs. */
@RunWith(AndroidJUnit4.class)
public class MetadataEngineTest {

  private String motionPhotoPath;
  private String agtmPhotoPath;
  private String photoOnlyPath;

  @Before
  public void setUp() {
    motionPhotoPath = TestUtil.getTestDataPath("motion_photo.MP.jpg");
    agtmPhotoPath = TestUtil.getTestDataPath("motion_photo_agtm.MP.jpg");
    photoOnlyPath = TestUtil.getTestDataPath("motion_photo_photo_only.jpg");
  }

  @Test
  public void parseMetadata_viaPath_returnsSerializedProto() {
    byte[] protoBytes = MetadataEngine.parseMetadata(motionPhotoPath);
    assertThat(protoBytes).isNotNull();
    assertThat(protoBytes.length).isGreaterThan(0);

    boolean isMotionPhoto = MetadataEngine.isMotionPhoto(protoBytes);
    assertThat(isMotionPhoto).isTrue();
  }

  @Test
  public void parseMetadataFd_matchesPathOutput() throws Exception {
    byte[] pathBytes = MetadataEngine.parseMetadata(motionPhotoPath);
    assertThat(pathBytes).isNotNull();

    File file = new File(motionPhotoPath);
    try (ParcelFileDescriptor pfd =
        ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)) {
      int fd = pfd.getFd();
      byte[] fdBytes = MetadataEngine.parseMetadataFd(fd);
      assertThat(fdBytes).isEqualTo(pathBytes);

      byte[] sliceBytes = MetadataEngine.parseMetadataFd(fd, 0, file.length());
      assertThat(sliceBytes).isEqualTo(pathBytes);
    }
  }

  @Test
  public void isMotionPhoto_withOptions_evaluatesCorrectly() {
    byte[] motionProto = MetadataEngine.parseMetadata(motionPhotoPath);
    assertThat(motionProto).isNotNull();

    boolean enable3p = MetadataEngine.isMotionPhoto(motionProto, false, new String[0]);
    assertThat(enable3p).isTrue();

    boolean disable3p = MetadataEngine.isMotionPhoto(motionProto, true, new String[0]);
    assertThat(disable3p).isTrue();

    byte[] nonMotionProto = MetadataEngine.parseMetadata(photoOnlyPath);
    if (nonMotionProto != null) {
      boolean isMp = MetadataEngine.isMotionPhoto(nonMotionProto);
      assertThat(isMp).isFalse();
    }
  }

  @Test
  public void parseMetadata_heicMotionPhoto_isMotionPhoto() throws Exception {
    String heicPath = TestUtil.getTestDataPath("motion_photo_single_video_track.MP.heic");

    byte[] pathBytes = MetadataEngine.parseMetadata(heicPath);
    assertThat(pathBytes).isNotNull();
    assertThat(MetadataEngine.isMotionPhoto(pathBytes)).isTrue();

    File file = new File(heicPath);
    try (ParcelFileDescriptor pfd =
        ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)) {
      byte[] fdBytes = MetadataEngine.parseMetadataFd(pfd.getFd(), 0, file.length());
      assertThat(fdBytes).isEqualTo(pathBytes);
    }
  }

  @Test
  public void parseMetadata_heicWithoutVideo_isNotMotionPhoto() {
    // The XMP says motion photo, but the file has no 'mpvd' video box.
    byte[] protoBytes =
        MetadataEngine.parseMetadata(TestUtil.getTestDataPath("motion_photo_photo_only.heic"));

    assertThat(protoBytes).isNotNull();
    assertThat(MetadataEngine.isMotionPhoto(protoBytes)).isFalse();
  }

  @Test
  public void extractAgtm_viaPathAndMemory_returnsValidJson() throws Exception {
    String agtmJsonPath = MetadataEngine.extractAgtm(agtmPhotoPath);
    assertThat(agtmJsonPath).isNotNull();
    assertThat(agtmJsonPath).isNotEmpty();
    assertThat(agtmJsonPath).contains("hdrReferenceWhite");

    byte[] fileBytes = Files.readAllBytes(Path.of(agtmPhotoPath));
    String agtmJsonMem = MetadataEngine.extractAgtm(fileBytes);
    assertThat(agtmJsonMem).isNotNull();
    assertThat(agtmJsonMem).isNotEmpty();
    assertThat(agtmJsonMem).isEqualTo(agtmJsonPath);
  }

  @Test
  public void extractAgtmFd_matchesPathOutput() throws Exception {
    String agtmJsonPath = MetadataEngine.extractAgtm(agtmPhotoPath);
    assertThat(agtmJsonPath).isNotNull();

    File file = new File(agtmPhotoPath);
    try (ParcelFileDescriptor pfd =
        ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)) {
      int fd = pfd.getFd();
      String agtmJsonFd = MetadataEngine.extractAgtmFd(fd);
      assertThat(agtmJsonFd).isEqualTo(agtmJsonPath);

      String agtmJsonSlice = MetadataEngine.extractAgtmFd(fd, 0, file.length());
      assertThat(agtmJsonSlice).isEqualTo(agtmJsonPath);
    }
  }

  @Test
  public void extractAgtmAtTimestamp_returnsValidJson() {
    String agtmJson = MetadataEngine.extractAgtmAtTimestamp(agtmPhotoPath, 0L);
    assertThat(agtmJson).isNotNull();
    assertThat(agtmJson).isNotEmpty();
    assertThat(agtmJson).contains("hdrReferenceWhite");
  }

  @Test
  public void extractMetadata_writesXmlFile() throws Exception {
    File tempMeta = File.createTempFile("extracted_meta_", ".xml");
    tempMeta.deleteOnExit();

    boolean success = MetadataEngine.extractMetadata(motionPhotoPath, tempMeta.getAbsolutePath());
    assertThat(success).isTrue();
    assertThat(tempMeta.exists()).isTrue();
    assertThat(tempMeta.length()).isGreaterThan(0L);
  }
}
