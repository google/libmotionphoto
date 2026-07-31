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

import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.webkit.MimeTypeMap;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/** Utility class for file operations in the sample app. */
public class StorageUtils {

  private StorageUtils() {}

  @SuppressWarnings("ContentResolverUri")
  public static String copyUriToTempFile(
      Context context, Uri uri, String prefix, String defaultSuffix) throws IOException {
    InputStream inputStream;
    if ("file".equals(uri.getScheme())) {
      inputStream = new FileInputStream(new File(uri.getPath()));
    } else {
      inputStream = context.getContentResolver().openInputStream(uri);
    }
    if (inputStream == null) {
      throw new IOException("Failed to open input stream for URI: " + uri);
    }
    String suffix = defaultSuffix;
    String mimeType = context.getContentResolver().getType(uri);
    if (mimeType != null) {
      String ext = MimeTypeMap.getSingleton().getExtensionFromMimeType(mimeType);
      if (ext != null) {
        suffix = "." + ext;
      }
    }
    File tempFile = File.createTempFile(prefix, suffix, context.getCacheDir());
    FileOutputStream outputStream = new FileOutputStream(tempFile);
    byte[] buffer = new byte[4096];
    int bytesRead;
    while ((bytesRead = inputStream.read(buffer)) != -1) {
      outputStream.write(buffer, 0, bytesRead);
    }
    inputStream.close();
    outputStream.close();
    return tempFile.getAbsolutePath();
  }

  @SuppressWarnings("ContentResolverUri")
  public static ParcelFileDescriptor openParcelFileDescriptor(Context context, Uri uri)
      throws IOException {
    if ("file".equals(uri.getScheme())) {
      return ParcelFileDescriptor.open(
          new File(uri.getPath()), ParcelFileDescriptor.MODE_READ_ONLY);
    }
    return context.getContentResolver().openFileDescriptor(uri, "r");
  }

  public static String generateXmpFile(Context context, long timestampUs) throws IOException {
    String xmpTemplate =
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Adobe XMP Core 5.1.0-jc003\">\n"
            + "  <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
            + "    <rdf:Description rdf:about=\"\"\n"
            + "      xmlns:Metadata=\"http://ns.google.com/1.0/motion_photo_video_metadata/\">\n"
            + "      <Metadata:Flags>\n"
            + "        <Metadata:LowRes Metadata:Stabilized=\"0\"/>\n"
            + "        <Metadata:HighRes Metadata:Stabilized=\"0\" />\n"
            + "      </Metadata:Flags>\n"
            + "      <Metadata:Scores Metadata:ModelVersion=\"1\">\n"
            + "        <Metadata:PrimaryImage>\n"
            + "          <Metadata:Frame Metadata:Score=\"1.0\" Metadata:Time=\"%d\" />\n"
            + "        </Metadata:PrimaryImage>\n"
            + "        <Metadata:HighResTrack>\n"
            + "          <rdf:Seq>\n"
            + "            <rdf:li>\n"
            + "              <Metadata:Frame Metadata:Score=\"1.0\" Metadata:Time=\"%d\" />\n"
            + "            </rdf:li>\n"
            + "          </rdf:Seq>\n"
            + "        </Metadata:HighResTrack>\n"
            + "      </Metadata:Scores>\n"
            + "    </rdf:Description>\n"
            + "  </rdf:RDF>\n"
            + "</x:xmpmeta>\n";
    String xmpContent = String.format(Locale.US, xmpTemplate, timestampUs, timestampUs);
    File tempFile = File.createTempFile("moments", ".xmp", context.getCacheDir());
    FileWriter writer = new FileWriter(tempFile);
    writer.write(xmpContent);
    writer.close();
    return tempFile.getAbsolutePath();
  }

  public static File copyAssetToCache(Context context, String assetName) throws IOException {
    File cacheFile = new File(context.getCacheDir(), assetName);
    if (cacheFile.exists()) {
      cacheFile.delete();
    }
    try (InputStream inputStream = context.getAssets().open(assetName);
        FileOutputStream outputStream = new FileOutputStream(cacheFile)) {
      byte[] buffer = new byte[8192];
      int bytesRead;
      while ((bytesRead = inputStream.read(buffer)) != -1) {
        outputStream.write(buffer, 0, bytesRead);
      }
    }
    return cacheFile;
  }

  public static void copyFileToStream(File sourceFile, OutputStream outputStream)
      throws IOException {
    try (FileInputStream inputStream = new FileInputStream(sourceFile)) {
      byte[] buffer = new byte[4096];
      int bytesRead;
      while ((bytesRead = inputStream.read(buffer)) != -1) {
        outputStream.write(buffer, 0, bytesRead);
      }
    } finally {
      outputStream.close();
    }
  }

  public static String getFileNameFromUri(Context context, Uri uri) {
    if (uri == null) {
      return "";
    }
    String scheme = uri.getScheme();
    if ("file".equals(scheme)) {
      String path = uri.getPath();
      if (path != null) {
        return new File(path).getName();
      }
    }

    if (context != null && "content".equals(scheme)) {
      // 1. Query OpenableColumns.DISPLAY_NAME directly for Scoped Storage (API 29+) compatibility
      try (android.database.Cursor cursor =
          context
              .getContentResolver()
              .query(
                  uri,
                  new String[] {android.provider.OpenableColumns.DISPLAY_NAME},
                  null,
                  null,
                  null)) {
        if (cursor != null && cursor.moveToFirst()) {
          int nameIdx = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
          if (nameIdx != -1) {
            String name = cursor.getString(nameIdx);
            if (name != null && !name.isEmpty() && !isNumericFilename(name)) {
              return name;
            }
          }
        }
      } catch (Exception ignored) {
      }

      // 2. Try resolving MediaStore content Uri via ID if Photopicker returned a numeric ID name
      String idStr = null;
      if (android.provider.DocumentsContract.isDocumentUri(context, uri)) {
        String docId = android.provider.DocumentsContract.getDocumentId(uri);
        if (docId != null && docId.contains(":")) {
          idStr = docId.split(":")[1];
        } else {
          idStr = docId;
        }
      } else {
        String lastSeg = uri.getLastPathSegment();
        if (lastSeg != null && lastSeg.matches("\\d+")) {
          idStr = lastSeg;
        }
      }

      if (idStr != null) {
        Uri mediaStoreUri = android.provider.MediaStore.Files.getContentUri("external");
        String selection = android.provider.MediaStore.Files.FileColumns._ID + "=?";
        String[] selectionArgs = new String[] {idStr};
        try (android.database.Cursor cursor =
            context
                .getContentResolver()
                .query(
                    mediaStoreUri,
                    new String[] {android.provider.MediaStore.Files.FileColumns.DISPLAY_NAME},
                    selection,
                    selectionArgs,
                    null)) {
          if (cursor != null && cursor.moveToFirst()) {
            int nameIdx =
                cursor.getColumnIndex(android.provider.MediaStore.Files.FileColumns.DISPLAY_NAME);
            if (nameIdx != -1) {
              String name = cursor.getString(nameIdx);
              if (name != null && !name.isEmpty() && !isNumericFilename(name)) {
                return name;
              }
            }
          }
        } catch (Exception ignored) {
        }
      }

      // 3. Query OpenableColumns.DISPLAY_NAME as fallback
      try (android.database.Cursor cursor =
          context
              .getContentResolver()
              .query(
                  uri,
                  new String[] {android.provider.OpenableColumns.DISPLAY_NAME},
                  null,
                  null,
                  null)) {
        if (cursor != null && cursor.moveToFirst()) {
          int nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
          if (nameIndex != -1) {
            String name = cursor.getString(nameIndex);
            if (name != null && !name.isEmpty()) {
              return name;
            }
          }
        }
      } catch (Exception ignored) {
      }
    }

    String lastSegment = uri.getLastPathSegment();
    return lastSegment != null ? lastSegment : uri.toString();
  }

  private static boolean isNumericFilename(String filename) {
    if (filename == null || filename.isEmpty()) return false;
    int dot = filename.lastIndexOf('.');
    String base = (dot != -1) ? filename.substring(0, dot) : filename;
    return base.matches("\\d+");
  }
}
