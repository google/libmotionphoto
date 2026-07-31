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

import com.google.libmotionphoto.motionphoto.MotionPhotoExtractor;

/**
 * Service that runs in an isolated process to extract motion photo components. This provides
 * sandboxing for the C++ motion photo extractor.
 */
public class MotionPhotoExtractorService extends Service {
  private static final String TAG = "MotionPhotoExtractorService";

  private final IMotionPhotoExtractorService.Stub binder =
      new IMotionPhotoExtractorService.Stub() {
        @Override
        @SuppressWarnings("CheckedExceptionNotThrown")
        public int extractMotionPhoto(
            String motionPhotoFile,
            String primaryImageOutput,
            String videoOutput,
            String metadataOutput)
            throws RemoteException {
          try {
            MotionPhotoExtractor.Params params = new MotionPhotoExtractor.Params();
            params.motionPhotoFile = motionPhotoFile;
            params.primaryImageOutput = primaryImageOutput;
            params.videoOutput = videoOutput;
            params.metadataOutput = metadataOutput;
            return MotionPhotoExtractor.extract(params, msg -> Log.i(TAG, "Extractor: " + msg));
          } catch (RuntimeException e) {
            Log.e(TAG, "Error in MotionPhotoExtractorService extractMotionPhoto", e);
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
