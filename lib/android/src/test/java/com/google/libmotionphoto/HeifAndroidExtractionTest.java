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

package com.google.libmotionphoto;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import com.google.libmotionphoto.motionphoto.MetadataEngine;
import com.google.libmotionphoto.motionphoto.MotionPhotoExtractor;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Collections;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

/**
 * Android environment unit tests (Robolectric SDK 34 + Android AIDL Services + JNI) verifying HEIC
 * Motion Photo metadata parsing and extraction (still image, video, and metadata) without libheif.
 */
@RunWith(AndroidJUnit4.class)
@Config(sdk = 34)
public class HeifAndroidExtractionTest {

  private Context context;
  private File heicMotionPhotoFile;
  private File heicPhotoOnlyFile;

  private static File resolveTestDataFile(String fileName) {
    String[] candidatePrefixes = {
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

  @Before
  public void setUp() {
    context = ApplicationProvider.getApplicationContext();
    heicMotionPhotoFile = resolveTestDataFile("motion_photo_single_video_track.MP.heic");
    heicPhotoOnlyFile = resolveTestDataFile("motion_photo_photo_only.heic");
  }

  @Test
  public void androidEnvironment_isRunningUnderAndroidSdk34() {
    assertEquals(34, Build.VERSION.SDK_INT);
    assertNotNull(context);
    assertNotNull(context.getCacheDir());
    assertTrue(context.getCacheDir().exists());
  }

  @Test
  public void metadataParserService_parsesHeicMotionPhotoViaParcelFileDescriptor()
      throws Exception {
    MetadataParserService serviceInstance = new MetadataParserService();
    IBinder binder = serviceInstance.onBind(new Intent());
    IMetadataParserService service = IMetadataParserService.Stub.asInterface(binder);
    assertNotNull(service);

    // 1. Valid .heic Motion Photo opened via Android ParcelFileDescriptor
    // (Note: MetadataParserService.parseMetadata closes the ParcelFileDescriptor in its finally block)
    ParcelFileDescriptor pfd =
        ParcelFileDescriptor.open(heicMotionPhotoFile, ParcelFileDescriptor.MODE_READ_ONLY);
    byte[] protoBytes = service.parseMetadata(pfd);
    assertNotNull(protoBytes);
    assertTrue(protoBytes.length > 0);
    assertTrue(service.isMotionPhoto(protoBytes, false, Collections.emptyList()));

    // 2. Still-only .heic (no mpvd video box) opened via Android ParcelFileDescriptor
    ParcelFileDescriptor photoOnlyPfd =
        ParcelFileDescriptor.open(heicPhotoOnlyFile, ParcelFileDescriptor.MODE_READ_ONLY);
    byte[] photoOnlyProtoBytes = service.parseMetadata(photoOnlyPfd);
    assertNotNull(photoOnlyProtoBytes);
    assertFalse(service.isMotionPhoto(photoOnlyProtoBytes, false, Collections.emptyList()));
  }

  @Test
  public void motionPhotoExtractorService_extractsHeicStillVideoAndMetadataInAndroidCacheDir()
      throws Exception {
    MotionPhotoExtractorService extractorServiceInstance = new MotionPhotoExtractorService();
    IBinder extractorBinder = extractorServiceInstance.onBind(new Intent());
    IMotionPhotoExtractorService extractorService =
        IMotionPhotoExtractorService.Stub.asInterface(extractorBinder);
    assertNotNull(extractorService);

    File cacheDir = context.getCacheDir();
    File outStillHeic = new File(cacheDir, "android_service_extracted_still.heic");
    File outVideoMp4 = new File(cacheDir, "android_service_extracted_video.mp4");
    File outMetadataXml = new File(cacheDir, "android_service_extracted_metadata.xml");
    outStillHeic.delete();
    outVideoMp4.delete();
    outMetadataXml.delete();

    int status =
        extractorService.extractMotionPhoto(
            heicMotionPhotoFile.getAbsolutePath(),
            outStillHeic.getAbsolutePath(),
            outVideoMp4.getAbsolutePath(),
            outMetadataXml.getAbsolutePath());
    assertEquals(0, status);

    // 1. Verify extracted .heic still image has exact size before 'mpvd' box (28,727 bytes)
    // and is no longer recognized as a Motion Photo by Android MetadataParserService.
    assertTrue(outStillHeic.exists());
    assertEquals(28727L, outStillHeic.length());

    MetadataParserService parserServiceInstance = new MetadataParserService();
    IMetadataParserService parserService =
        IMetadataParserService.Stub.asInterface(parserServiceInstance.onBind(new Intent()));
    ParcelFileDescriptor extractedStillPfd =
        ParcelFileDescriptor.open(outStillHeic, ParcelFileDescriptor.MODE_READ_ONLY);
    byte[] extractedStillProto = parserService.parseMetadata(extractedStillPfd);
    assertNotNull(extractedStillProto);
    assertFalse(parserService.isMotionPhoto(extractedStillProto, false, Collections.emptyList()));

    // 2. Verify extracted .mp4 video file
    assertTrue(outVideoMp4.exists());
    assertEquals(122038L, outVideoMp4.length());

    // 3. Verify extracted .xml XMP metadata file
    assertTrue(outMetadataXml.exists());
    String xmpContent =
        new String(Files.readAllBytes(outMetadataXml.toPath()), StandardCharsets.UTF_8);
    assertTrue(xmpContent.contains("MotionPhoto"));
    assertTrue(xmpContent.contains("image/heic"));

    outStillHeic.delete();
    outVideoMp4.delete();
    outMetadataXml.delete();
  }

  @Test
  public void motionPhotoExtractorAndMetadataEngine_extractHeicDirectlyViaJni() throws Exception {
    File cacheDir = context.getCacheDir();
    File outStillHeic = new File(cacheDir, "android_jni_extracted_still.heic");
    File outVideoMp4 = new File(cacheDir, "android_jni_extracted_video.mp4");
    File outMetadataXml = new File(cacheDir, "android_jni_extracted_metadata.xml");
    File outEngineMetaXml = new File(cacheDir, "android_engine_extracted_metadata.xml");

    MotionPhotoExtractor.Params params = new MotionPhotoExtractor.Params();
    params.motionPhotoFile = heicMotionPhotoFile.getAbsolutePath();
    params.primaryImageOutput = outStillHeic.getAbsolutePath();
    params.videoOutput = outVideoMp4.getAbsolutePath();
    params.metadataOutput = outMetadataXml.getAbsolutePath();

    StringBuilder logs = new StringBuilder();
    int result = MotionPhotoExtractor.extract(params, logs::append);
    assertEquals(0, result);
    assertTrue(outStillHeic.exists());
    assertEquals(28727L, outStillHeic.length());
    assertTrue(outVideoMp4.exists());
    assertTrue(outVideoMp4.length() > 0L);
    assertTrue(outMetadataXml.exists());
    assertTrue(outMetadataXml.length() > 0L);

    assertTrue(
        MetadataEngine.extractMetadata(
            heicMotionPhotoFile.getAbsolutePath(), outEngineMetaXml.getAbsolutePath()));
    assertTrue(outEngineMetaXml.exists());
    assertTrue(outEngineMetaXml.length() > 0L);

    outStillHeic.delete();
    outVideoMp4.delete();
    outMetadataXml.delete();
    outEngineMetaXml.delete();
  }
}
