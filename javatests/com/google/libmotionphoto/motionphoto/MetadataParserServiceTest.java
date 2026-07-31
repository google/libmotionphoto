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
import android.os.ParcelFileDescriptor;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import com.google.libmotionphoto.MetadataParserService;
import com.google.libmotionphoto.IMetadataParserService;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Unit tests for MetadataParserService and its sandboxed AIDL IPC interface. */
@RunWith(AndroidJUnit4.class)
public class MetadataParserServiceTest {

  private IMetadataParserService service;
  private String motionPhotoPath;
  private String agtmPhotoPath;

  @Before
  public void setUp() {
    MetadataParserService serviceInstance = new MetadataParserService();
    IBinder binder = serviceInstance.onBind(new Intent());
    service = IMetadataParserService.Stub.asInterface(binder);

    motionPhotoPath = TestUtil.getTestDataPath("motion_photo.MP.jpg");
    agtmPhotoPath = TestUtil.getTestDataPath("motion_photo_agtm.MP.jpg");
  }

  @Test
  public void parseMetadata_viaPfd_returnsSerializedProto() throws Exception {
    try (ParcelFileDescriptor pfd =
        ParcelFileDescriptor.open(new File(motionPhotoPath), ParcelFileDescriptor.MODE_READ_ONLY)) {
      byte[] protoBytes = service.parseMetadata(pfd);
      assertThat(protoBytes).isNotNull();
      assertThat(protoBytes.length).isGreaterThan(0);

      List<String> enabled3p = new ArrayList<>();
      boolean isMotionPhoto = service.isMotionPhoto(protoBytes, false, enabled3p);
      assertThat(isMotionPhoto).isTrue();
    }
  }

  @Test
  public void extractAgtm_viaPfd_returnsAgtmJson() throws Exception {
    try (ParcelFileDescriptor pfd =
        ParcelFileDescriptor.open(new File(agtmPhotoPath), ParcelFileDescriptor.MODE_READ_ONLY)) {
      String agtmJson = service.extractAgtm(pfd);
      assertThat(agtmJson).isNotNull();
      assertThat(agtmJson).isNotEmpty();
      assertThat(agtmJson).contains("hdrReferenceWhite");
    }
  }

  @Test
  public void parseMetadata_nullPfd_returnsNull() throws Exception {
    byte[] result = service.parseMetadata(null);
    assertThat(result).isNull();
  }

  @Test
  public void isMotionPhoto_nullProto_returnsFalse() throws Exception {
    boolean result = service.isMotionPhoto(null, false, new ArrayList<>());
    assertThat(result).isFalse();
  }
}
