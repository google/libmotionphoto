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

package com.google.libmotionphoto.motionphoto.playground;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.ProviderInfo;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowContentResolver;

/** Android Robolectric tests for StorageUtils HEIC/AVIF file extension preservation. */
@RunWith(AndroidJUnit4.class)
@Config(sdk = 34)
public class StorageUtilsTest {

  private static File resolveTestDataFile(String fileName) {
    String[] candidatePrefixes = {
      "../../../lib/core/motion_photo/testdata/",
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

  @Test
  public void copyUriToTempFile_preservesHeicExtensionInAndroidCacheDir() throws Exception {
    Context context = ApplicationProvider.getApplicationContext();
    File srcHeic = resolveTestDataFile("motion_photo_single_video_track.MP.heic");
    Uri fileUri = Uri.fromFile(srcHeic);

    String copiedPath =
        StorageUtils.copyUriToTempFile(context, fileUri, "picked_motion_photo", ".jpg");
    assertNotNull(copiedPath);
    File copied = new File(copiedPath);
    assertTrue(copied.exists());
    assertTrue(
        "Expected .heic suffix, got: " + copied.getName(), copied.getName().endsWith(".heic"));
    assertEquals(srcHeic.length(), copied.length());
    copied.delete();
  }

  @Test
  public void copyUriToTempFile_preservesAvifAndGenericMimeFallsBackToDisplayName()
      throws Exception {
    Context context = ApplicationProvider.getApplicationContext();
    File backingFile = File.createTempFile("backing", ".bin", context.getCacheDir());
    try (FileOutputStream fos = new FileOutputStream(backingFile)) {
      fos.write("avif-payload".getBytes(StandardCharsets.UTF_8));
    }

    String authority = "com.google.libmotionphoto.test.generic";
    ContentProvider provider =
        new ContentProvider() {
          @Override
          public boolean onCreate() {
            return true;
          }

          @Override
          public String getType(Uri uri) {
            return uri.getPath().contains("wildcard") ? "image/*" : "application/octet-stream";
          }

          @Override
          public Cursor query(
              Uri uri,
              String[] projection,
              String selection,
              String[] selectionArgs,
              String sortOrder) {
            MatrixCursor cursor = new MatrixCursor(new String[] {OpenableColumns.DISPLAY_NAME});
            cursor.addRow(
                new Object[] {
                  uri.getPath().contains("wildcard") ? "photo.heic" : "photo.avif"
                });
            return cursor;
          }

          @Override
          public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
            return ParcelFileDescriptor.open(backingFile, ParcelFileDescriptor.MODE_READ_ONLY);
          }

          @Override
          public Uri insert(Uri uri, ContentValues values) {
            return null;
          }

          @Override
          public int delete(Uri uri, String selection, String[] selectionArgs) {
            return 0;
          }

          @Override
          public int update(
              Uri uri, ContentValues values, String selection, String[] selectionArgs) {
            return 0;
          }
        };
    ProviderInfo providerInfo = new ProviderInfo();
    providerInfo.authority = authority;
    provider.attachInfo(context, providerInfo);
    ShadowContentResolver.registerProviderInternal(authority, provider);

    Uri avifUri = Uri.parse("content://" + authority + "/item1");
    String avifCopied = StorageUtils.copyUriToTempFile(context, avifUri, "generic_avif", ".jpg");
    File avifFile = new File(avifCopied);
    assertTrue("Expected .avif suffix, got: " + avifFile.getName(), avifFile.getName().endsWith(".avif"));
    avifFile.delete();

    Uri heicUri = Uri.parse("content://" + authority + "/wildcard");
    String heicCopied = StorageUtils.copyUriToTempFile(context, heicUri, "wildcard_heic", ".jpg");
    File heicFile = new File(heicCopied);
    assertTrue("Expected .heic suffix, got: " + heicFile.getName(), heicFile.getName().endsWith(".heic"));
    heicFile.delete();
    backingFile.delete();
  }

  @Test
  public void copyUriToTempFile_cleansUpTempFileOnOpenFailure() {
    Context context = ApplicationProvider.getApplicationContext();
    Uri brokenUri = Uri.parse("content://com.google.libmotionphoto.missing/broken.heic");
    boolean threw = false;
    try {
      StorageUtils.copyUriToTempFile(context, brokenUri, "broken_prefix_", ".jpg");
    } catch (IOException | RuntimeException e) {
      threw = true;
    }
    assertTrue(threw);
    File[] leftovers =
        context.getCacheDir().listFiles((dir, name) -> name.startsWith("broken_prefix_"));
    assertEquals(0, leftovers != null ? leftovers.length : 0);
  }

  @Test
  public void getMimeType_and_getFileExtension_handleHeicAvifAndEdgeCases() {
    assertEquals("image/heic", StorageUtils.getMimeType(new File("a.heic")));
    assertEquals("image/heic", StorageUtils.getMimeType(new File("a.HEIF")));
    assertEquals("image/avif", StorageUtils.getMimeType(new File("a.avif")));
    assertEquals("image/jpeg", StorageUtils.getMimeType(new File("a.jpg")));
    assertEquals("video/mp4", StorageUtils.getMimeType(new File("a.mp4")));
    assertEquals("text/xml", StorageUtils.getMimeType(new File("a.xml")));
    assertEquals("*/*", StorageUtils.getMimeType(new File("noext")));
    assertEquals("*/*", StorageUtils.getMimeType(null));

    assertEquals(".jpg", StorageUtils.getFileExtension("/v1.0/dir/noext", ".jpg"));
    assertEquals(".avif", StorageUtils.getFileExtension("/dir/photo.AVIF", ".jpg"));
    assertEquals(".jpg", StorageUtils.getFileExtension("", ".jpg"));
  }
}
