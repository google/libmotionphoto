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

/**
 * Service interface for extracting motion photo components in an isolated process.
 */
interface IMotionPhotoExtractorService {
    /**
     * Extracts individual assets from a Motion Photo container.
     *
     * @param motionPhotoFile The input motion photo file path.
     * @param primaryImageOutput The output path for extracted primary image.
     * @param videoOutput The output path for extracted video track.
     * @param metadataOutput The output path for extracted metadata XML.
     * @return 0 on success, or non-zero error code.
     */
    int extractMotionPhoto(
        in String motionPhotoFile,
        in String primaryImageOutput,
        in String videoOutput,
        in String metadataOutput);
}
