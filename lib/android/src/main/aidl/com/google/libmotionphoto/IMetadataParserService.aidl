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

import android.os.ParcelFileDescriptor;
import java.util.List;

/**
 * Service interface for parsing motion photo metadata in an isolated process.
 */
interface IMetadataParserService {
    /**
     * Parses metadata from the given ParcelFileDescriptor.
     *
     * @param pfd The ParcelFileDescriptor of the motion photo or image file.
     * @return Serialized MetadataCollection proto bytes, or null on error.
     */
    byte[] parseMetadata(in ParcelFileDescriptor pfd);

    /**
     * Helper to check if the serialized MetadataCollection represents a motion photo.
     *
     * @param serializedCollection Serialized MetadataCollection proto bytes.
     * @param disable3pPlugins Whether to disable 3P metadata plugins.
     * @param enabled3pPlugins List of enabled 3P metadata plugin names.
     * @return true if it is a motion photo, false otherwise.
     */
    boolean isMotionPhoto(
        in byte[] serializedCollection,
        boolean disable3pPlugins,
        in List<String> enabled3pPlugins);

    /**
     * Extracts AGTM (SMPTE ST 2094-50) metadata from the given ParcelFileDescriptor.
     *
     * @param pfd The ParcelFileDescriptor of the motion photo or video file.
     * @return JSON string containing AGTM metadata, or null on error/not found.
     */
    String extractAgtm(in ParcelFileDescriptor pfd);
}
