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
import android.os.RemoteException;
import android.util.Log;

import com.google.libmotionphoto.motionphoto.MotionPhotoBuilder;
import java.io.File;
import java.io.IOException;

/**
 * Service that runs in an isolated process to build motion photos. This provides sandboxing for the
 * C++ motion photo builder.
 */
public class MotionPhotoBuilderService extends Service {
  private static final String TAG = "MotionPhotoBuilderService";

  private final IMotionPhotoBuilderService.Stub binder =
      new IMotionPhotoBuilderService.Stub() {
        @Override
        @SuppressWarnings("CheckedExceptionNotThrown")
        public int buildMotionPhoto(
            String imagePath,
            String videoPath,
            String momentsVideoPath,
            String momentsXmp,
            long presentationTimestampUs,
            String outputImagePath)
            throws RemoteException {
          File workingVideo = null;
          File optVideo = null;
          try {
            MotionPhotoBuilder.Params params = new MotionPhotoBuilder.Params();
            params.primaryImage = imagePath;
            params.primaryVideo = videoPath;
            params.momentsVideo = momentsVideoPath;
            params.momentsXmp = momentsXmp;
            params.presentationTimestampUs = presentationTimestampUs;
            params.outputImage = outputImagePath;

            File cacheDir = null;
            try {
              cacheDir = getCacheDir();
            } catch (RuntimeException ignored) {
              // Ignored when service is instantiated directly in unit tests without context.
            }
            if (cacheDir == null) {
              cacheDir = new File(System.getProperty("java.io.tmpdir", "/tmp"));
            }

            try {
              workingVideo = File.createTempFile("motion_photo_working_trailer", ".mp4", cacheDir);
              optVideo = File.createTempFile("motion_photo_optimized_trailer", ".mp4", cacheDir);
              params.workingVideo = workingVideo.getAbsolutePath();
              params.optimizedVideo = optVideo.getAbsolutePath();
            } catch (IOException e) {
              Log.w(TAG, "Failed to create working video temp files in cache", e);
            }

            return MotionPhotoBuilder.build(params, msg -> Log.i(TAG, "Builder: " + msg));
          } catch (RuntimeException e) {
            Log.e(TAG, "Error in MotionPhotoBuilderService buildMotionPhoto", e);
            return -1;
          } finally {
            if (workingVideo != null && workingVideo.exists()) {
              workingVideo.delete();
            }
            if (optVideo != null && optVideo.exists()) {
              optVideo.delete();
            }
          }
        }

        @Override
        @SuppressWarnings("CheckedExceptionNotThrown")
        public int buildMotionPhotoFromMemory(
            byte[] imageBytes, byte[] videoBytes, String momentsXmp, String outputImagePath)
            throws RemoteException {
          try {
            return MotionPhotoBuilder.buildFromMemory(
                imageBytes,
                videoBytes,
                momentsXmp,
                outputImagePath,
                msg -> Log.i(TAG, "BuilderFromMemory: " + msg));
          } catch (RuntimeException e) {
            Log.e(TAG, "Error in MotionPhotoBuilderService buildMotionPhotoFromMemory", e);
            return -1;
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
