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

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import androidx.annotation.Nullable;

import com.google.libmotionphoto.motionphoto.MetadataEngine;
import com.google.libmotionphoto.motionphoto.proto.MetadataBlock;
import com.google.libmotionphoto.motionphoto.proto.MetadataCollection;
import com.google.protobuf.ExtensionRegistryLite;
import java.io.IOException;
import java.util.List;

/**
 * Service that runs in an isolated process to parse motion photo metadata. This provides sandboxing
 * for the C++ parser.
 */
public class MetadataParserService extends Service {
  private static final String TAG = "MetadataParserService";

  /**
   * Validates that the metadata collection returned from the sandboxed C++ parser contains valid
   * non-negative offsets and lengths that fall within file size bounds.
   */
  private static boolean validateMetadataCollection(byte[] serializedCollection, long fileSize) {
    if (serializedCollection == null) {
      return false;
    }
    try {
      MetadataCollection collection =
          MetadataCollection.parseFrom(
              serializedCollection, ExtensionRegistryLite.getEmptyRegistry());
      Log.i(
          TAG,
          "validateMetadataCollection: "
              + collection.getBlocksCount()
              + " blocks, fileSize: "
              + fileSize);
      for (MetadataBlock block : collection.getBlocksList()) {
        Log.i(
            TAG,
            "Block ["
                + block.getFormatIdentifier()
                + "] offset="
                + block.getOffset()
                + " vidOffset="
                + (block.hasVideoOffsetInBlock() ? block.getVideoOffsetInBlock() : "none")
                + " vidLen="
                + (block.hasVideoLength() ? block.getVideoLength() : "none"));
        if (block.getOffset() < 0 || (fileSize > 0 && block.getOffset() > fileSize)) {
          Log.e(
              TAG,
              "Invalid metadata block offset: " + block.getOffset() + " > fileSize " + fileSize);
          return false;
        }
        if (block.hasVideoOffsetInBlock() && block.getVideoOffsetInBlock() < 0) {
          Log.e(TAG, "Invalid video offset in block: " + block.getVideoOffsetInBlock());
          return false;
        }
        if (block.hasVideoLength() && block.getVideoLength() < 0) {
          Log.e(TAG, "Invalid video length: " + block.getVideoLength());
          return false;
        }
        if (fileSize > 0 && block.hasVideoOffsetInBlock() && block.hasVideoLength()) {
          long totalOffset = block.getOffset() + block.getVideoOffsetInBlock();
          if (totalOffset + block.getVideoLength() > fileSize) {
            Log.e(
                TAG,
                "Video bounds exceed file size: totalOffset="
                    + totalOffset
                    + " + vidLen="
                    + block.getVideoLength()
                    + " = "
                    + (totalOffset + block.getVideoLength())
                    + " > fileSize "
                    + fileSize);
            return false;
          }
        }
      }
      return true;
    } catch (Exception e) {
      Log.e(TAG, "Failed to validate MetadataCollection proto payload", e);
      return false;
    }
  }

  private final IMetadataParserService.Stub binder =
      new IMetadataParserService.Stub() {
        @Override
        @Nullable
        @SuppressWarnings("CheckedExceptionNotThrown")
        public byte[] parseMetadata(ParcelFileDescriptor pfd) throws RemoteException {
          if (pfd == null) {
            Log.e(TAG, "Received null ParcelFileDescriptor");
            return null;
          }
          try {
            int fd = pfd.getFd();
            long fileSize = pfd.getStatSize();
            Log.i(TAG, "Parsing metadata from FD: " + fd + ", size: " + fileSize);
            byte[] result = MetadataEngine.parseMetadataFd(fd, 0, fileSize);
            if (!validateMetadataCollection(result, fileSize)) {
              Log.e(TAG, "Metadata collection validation failed for FD: " + fd);
              return null;
            }
            return result;
          } finally {
            try {
              pfd.close();
            } catch (IOException e) {
              Log.e(TAG, "Failed to close ParcelFileDescriptor", e);
            }
          }
        }

        @Override
        @SuppressWarnings("CheckedExceptionNotThrown")
        public boolean isMotionPhoto(
            byte[] serializedCollection, boolean disable3pPlugins, List<String> enabled3pPlugins)
            throws RemoteException {
          if (!validateMetadataCollection(serializedCollection, -1)) {
            return false;
          }
          String[] enabled3pArray =
              enabled3pPlugins != null ? enabled3pPlugins.toArray(new String[0]) : new String[0];
          return MetadataEngine.isMotionPhoto(
              serializedCollection, disable3pPlugins, enabled3pArray);
        }

        @Override
        @Nullable
        @SuppressWarnings("CheckedExceptionNotThrown")
        public String extractAgtm(ParcelFileDescriptor pfd) throws RemoteException {
          if (pfd == null) {
            return null;
          }
          try {
            int inFd = pfd.getFd();
            long fileSize = pfd.getStatSize();
            return MetadataEngine.extractAgtmFd(inFd, 0, fileSize);
          } catch (RuntimeException e) {
            Log.e(TAG, "Error in MetadataParserService extractAgtm", e);
            return null;
          } finally {
            try {
              pfd.close();
            } catch (IOException e) {
              Log.e(TAG, "Failed to close ParcelFileDescriptor in extractAgtm", e);
            }
          }
        }
      };

  @Override
  public void onCreate() {
    super.onCreate();
    Log.i(TAG, "Service created");
  }

  @Override
  public IBinder onBind(Intent intent) {
    Log.i(TAG, "Service bound");
    return binder;
  }
}
