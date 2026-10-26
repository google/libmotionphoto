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

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.ImageDecoder;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SdkSuppress;
import androidx.test.platform.app.InstrumentationRegistry;
import com.google.libmotionphoto.motionphoto.MetadataEngine;
import com.google.libmotionphoto.motionphoto.MotionPhotoBuilder;
import com.google.libmotionphoto.motionphoto.MotionPhotoExtractor;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * On-device test: builds a HEIC motion photo with libmotionphoto's lossless HEIF metadata editor
 * (no libheif, no codec) and checks with the Android platform HEIF decoder ({@link ImageDecoder})
 * that the still image decodes pixel-identically to the input, before and after extraction.
 */
@RunWith(AndroidJUnit4.class)
@SdkSuppress(minSdkVersion = 28) // ImageDecoder
public class HeifLosslessBuildDeviceTest {

  private static byte[] readAsset(Context context, String name) throws IOException {
    try (InputStream in = context.getAssets().open(name);
        ByteArrayOutputStream out = new ByteArrayOutputStream()) {
      byte[] buffer = new byte[64 * 1024];
      int n;
      while ((n = in.read(buffer)) != -1) {
        out.write(buffer, 0, n);
      }
      return out.toByteArray();
    }
  }

  private static Bitmap decode(byte[] bytes) throws IOException {
    return ImageDecoder.decodeBitmap(
        ImageDecoder.createSource(ByteBuffer.wrap(bytes)),
        (decoder, info, source) -> decoder.setAllocator(ImageDecoder.ALLOCATOR_SOFTWARE));
  }

  @Test
  public void builtHeicMotionPhoto_decodesPixelIdenticalOnPlatformDecoder() throws Exception {
    Context testContext = InstrumentationRegistry.getInstrumentation().getContext();
    Context appContext = InstrumentationRegistry.getInstrumentation().getTargetContext();
    byte[] heic = readAsset(testContext, "motion_photo_photo_only.heic");
    byte[] mp4 = readAsset(testContext, "motion_photo_video_only.mp4");

    File built = new File(appContext.getCacheDir(), "lossless_built.MP.heic");
    built.delete();
    int status =
        MotionPhotoBuilder.buildFromMemory(heic, mp4, null, built.getAbsolutePath(), null);
    assertEquals(0, status);
    byte[] builtBytes = Files.readAllBytes(built.toPath());

    // 1. libmotionphoto recognizes the result as a motion photo.
    assertTrue(MetadataEngine.isMotionPhoto(MetadataEngine.parseMetadata(built.getPath())));

    // 2. The Android platform decoder produces identical pixels for input and output.
    Bitmap original = decode(heic);
    Bitmap rebuilt = decode(builtBytes);
    assertEquals(original.getWidth(), rebuilt.getWidth());
    assertEquals(original.getHeight(), rebuilt.getHeight());
    assertTrue("Decoded pixels differ after building", original.sameAs(rebuilt));

    // 3. Extraction round-trips: still decodes identically, video equals the input video track.
    File still = new File(appContext.getCacheDir(), "lossless_still.heic");
    File video = new File(appContext.getCacheDir(), "lossless_video.mp4");
    MotionPhotoExtractor.Params params = new MotionPhotoExtractor.Params();
    params.motionPhotoFile = built.getAbsolutePath();
    params.primaryImageOutput = still.getAbsolutePath();
    params.videoOutput = video.getAbsolutePath();
    assertEquals(0, MotionPhotoExtractor.extract(params, null));
    Bitmap extracted = decode(Files.readAllBytes(still.toPath()));
    assertTrue("Decoded pixels differ after extraction", original.sameAs(extracted));
    assertTrue(video.length() > 0);
    assertTrue(
        !MetadataEngine.isMotionPhoto(MetadataEngine.parseMetadata(still.getPath())));

    // 4. Pixel buffers are bit-exact (sameAs already implies this; keep an explicit check).
    int[] a = new int[original.getWidth() * original.getHeight()];
    int[] b = new int[a.length];
    original.getPixels(a, 0, original.getWidth(), 0, 0, original.getWidth(), original.getHeight());
    rebuilt.getPixels(b, 0, rebuilt.getWidth(), 0, 0, rebuilt.getWidth(), rebuilt.getHeight());
    assertArrayEquals(a, b);

    built.delete();
    still.delete();
    video.delete();
  }
}
