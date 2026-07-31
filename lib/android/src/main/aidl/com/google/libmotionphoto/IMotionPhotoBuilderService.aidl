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
 * Service interface for building motion photos in an isolated process.
 */
interface IMotionPhotoBuilderService {
    /**
     * Assembles a Motion Photo container from input files.
     *
     * @param imagePath Path to the primary image file.
     * @param videoPath Path to the primary video file.
     * @param momentsVideoPath Optional moments video file path (can be null).
     * @param momentsXmp Optional moments XMP metadata XML string or file path (can be null).
     * @param presentationTimestampUs Presentation timestamp in microseconds (-1 for default).
     * @param outputImagePath The output motion photo file path.
     * @return 0 on success, or non-zero error code.
     */
    int buildMotionPhoto(
        in String imagePath,
        in String videoPath,
        in String momentsVideoPath,
        in String momentsXmp,
        long presentationTimestampUs,
        in String outputImagePath);

    /**
     * Assembles a Motion Photo container directly from in-memory byte buffers in a single pass.
     *
     * @param imageBytes The primary image bytes.
     * @param videoBytes The primary video bytes.
     * @param momentsXmp Optional moments XMP metadata XML string (can be null).
     * @param outputImagePath The output motion photo file path.
     * @return 0 on success, or non-zero error code.
     */
    int buildMotionPhotoFromMemory(
        in byte[] imageBytes,
        in byte[] videoBytes,
        in String momentsXmp,
        in String outputImagePath);
}
