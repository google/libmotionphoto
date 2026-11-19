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
    String suffix = resolveSuffix(context, uri, defaultSuffix);
    File tempFile = File.createTempFile(prefix, suffix, context.getCacheDir());
    try (InputStream inputStream = "file".equals(uri.getScheme())
            ? new FileInputStream(new File(uri.getPath()))
            : context.getContentResolver().openInputStream(uri)) {
      if (inputStream == null) {
        throw new IOException("Failed to open input stream for URI: " + uri);
      }
      try (FileOutputStream outputStream = new FileOutputStream(tempFile)) {
        byte[] buffer = new byte[4096];
        int bytesRead;
        while ((bytesRead = inputStream.read(buffer)) != -1) {
          outputStream.write(buffer, 0, bytesRead);
        }
      }
    } catch (IOException | RuntimeException e) {
      tempFile.delete();
      throw e;
    }
    return tempFile.getAbsolutePath();
  }

  private static String resolveSuffix(Context context, Uri uri, String defaultSuffix) {
    String mimeType = context.getContentResolver().getType(uri);
    if (mimeType != null) {
      if (mimeType.equalsIgnoreCase("image/heic") || mimeType.equalsIgnoreCase("image/heif")) {
        return ".heic";
      }
      if (mimeType.equalsIgnoreCase("image/avif")) {
        return ".avif";
      }
      if (!mimeType.equalsIgnoreCase("application/octet-stream") && !mimeType.endsWith("/*")) {
        String ext = MimeTypeMap.getSingleton().getExtensionFromMimeType(mimeType);
        if (ext != null && !ext.isEmpty() && !ext.equalsIgnoreCase("bin")) {
          return "." + ext.toLowerCase(Locale.US);
        }
      }
    }
    String fileName = getFileNameFromUri(context, uri);
    return getFileExtension(fileName, defaultSuffix);
  }

  /** Returns the lowercase dot-prefixed file extension, or {@code fallback} if missing. */
  public static String getFileExtension(String path, String fallback) {
    if (path == null || path.isEmpty()) {
      return fallback;
    }
    int lastSep = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
    int lastDot = path.lastIndexOf('.');
    if (lastDot > lastSep && lastDot < path.length() - 1) {
      return path.substring(lastDot).toLowerCase(Locale.US);
    }
    return fallback;
  }

  /** Returns the MIME type for {@code file}, falling back cleanly for HEIC/AVIF/MP4/XML. */
  public static String getMimeType(File file) {
    if (file == null) {
      return "*/*";
    }
    String dotExt = getFileExtension(file.getName(), "");
    String ext = dotExt.isEmpty() ? "" : dotExt.substring(1);
    if (ext.isEmpty()) {
      String urlExt = MimeTypeMap.getFileExtensionFromUrl(file.getAbsolutePath());
      ext = urlExt != null ? urlExt.toLowerCase(Locale.US) : "";
    }
    switch (ext) {
      case "heic":
      case "heif":
        return "image/heic";
      case "avif":
        return "image/avif";
      case "jpg":
      case "jpeg":
        return "image/jpeg";
      case "mp4":
        return "video/mp4";
      case "xml":
        return "text/xml";
      default:
        break;
    }
    if (!ext.isEmpty()) {
      String resolved = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext);
      if (resolved != null && !resolved.isEmpty()) {
        return resolved;
      }
    }
    return "*/*";
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
