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

public class MotionPhotoBuilder {
  public static class Params {
    public String primaryImage;
    public String primaryVideo;
    public String momentsVideo;
    public String momentsXmp;
    public String outputImage;
    public String workingVideo;
    public String optimizedVideo;
    public long presentationTimestampUs = -1;
  }

  public static int build(Params params, MotionPhotoJni.OutputCallback callback) {
    return MotionPhotoJni.builder(params, callback);
  }

  public static int buildFromMemory(
      byte[] imageBytes,
      byte[] videoBytes,
      String momentsXmp,
      String outputImageFile,
      MotionPhotoJni.OutputCallback callback) {
    return MotionPhotoJni.builderFromMemory(
        imageBytes, videoBytes, momentsXmp, outputImageFile, callback);
  }
}
