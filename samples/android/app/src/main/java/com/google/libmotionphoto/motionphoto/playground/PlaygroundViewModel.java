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

import android.net.Uri;
import android.util.Log;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;
import java.io.File;

/** Shared ViewModel for the Motion Photo Playground sample app. */
public class PlaygroundViewModel extends ViewModel {
  private static final String TAG = "PlaygroundVM";
  private final MutableLiveData<Uri> imageUri = new MutableLiveData<>();
  private final MutableLiveData<Uri> videoUri = new MutableLiveData<>();
  private final MutableLiveData<Uri> extractPhotoUri = new MutableLiveData<>();

  private final MutableLiveData<String> creatorLogs = new MutableLiveData<>("");
  private final MutableLiveData<String> extractorLogs = new MutableLiveData<>("");
  private final MutableLiveData<String> extractorStatus = new MutableLiveData<>("Extracting...");

  private final MutableLiveData<File> extractedVideoFile = new MutableLiveData<>();
  private final MutableLiveData<File> extractorExtractedImageFile = new MutableLiveData<>();
  private final MutableLiveData<File> extractorExtractedVideoFile = new MutableLiveData<>();
  private final MutableLiveData<File> extractorExtractedMetadataFile = new MutableLiveData<>();
  private final MutableLiveData<Boolean> disable3pPlugins = new MutableLiveData<>(true);
  private final MutableLiveData<File> builtMotionPhotoFile = new MutableLiveData<>();
  private final MutableLiveData<Long> timestampUs = new MutableLiveData<>(0L);

  public LiveData<Long> getTimestampUs() {
    return timestampUs;
  }

  public void setTimestampUs(long timestamp) {
    timestampUs.postValue(timestamp);
  }

  public LiveData<Boolean> getDisable3pPlugins() {
    return disable3pPlugins;
  }

  public void setDisable3pPlugins(boolean disable) {
    disable3pPlugins.postValue(disable);
  }

  public LiveData<Uri> getImageUri() {
    return imageUri;
  }

  public void setImageUri(Uri uri) {
    imageUri.postValue(uri);
  }

  public LiveData<Uri> getVideoUri() {
    return videoUri;
  }

  public void setVideoUri(Uri uri) {
    videoUri.postValue(uri);
  }

  public LiveData<Uri> getExtractPhotoUri() {
    return extractPhotoUri;
  }

  public void setExtractPhotoUri(Uri uri) {
    extractPhotoUri.postValue(uri);
  }

  public LiveData<String> getCreatorLogs() {
    return creatorLogs;
  }

  public void setCreatorLogs(String logs) {
    creatorLogs.postValue(logs);
  }

  public void appendCreatorLog(String log) {
    Log.d(TAG, "Creator: " + log);
    creatorLogs.postValue(creatorLogs.getValue() + log + "\n");
  }

  public void clearCreatorLogs() {
    creatorLogs.postValue("");
  }

  public LiveData<String> getExtractorLogs() {
    return extractorLogs;
  }

  public void setExtractorLogs(String logs) {
    extractorLogs.postValue(logs);
  }

  public void appendExtractorLog(String log) {
    Log.d(TAG, "Extractor: " + log);
    extractorLogs.postValue(extractorLogs.getValue() + log + "\n");
  }

  public void clearExtractorLogs() {
    extractorLogs.postValue("");
  }

  public LiveData<String> getExtractorStatus() {
    return extractorStatus;
  }

  public void setExtractorStatus(String status) {
    extractorStatus.postValue(status);
  }

  public LiveData<File> getExtractedVideoFile() {
    return extractedVideoFile;
  }

  public void setExtractedVideoFile(File file) {
    extractedVideoFile.postValue(file);
  }

  public LiveData<File> getExtractorExtractedImageFile() {
    return extractorExtractedImageFile;
  }

  public void setExtractorExtractedImageFile(File file) {
    extractorExtractedImageFile.postValue(file);
  }

  public LiveData<File> getExtractorExtractedVideoFile() {
    return extractorExtractedVideoFile;
  }

  public void setExtractorExtractedVideoFile(File file) {
    extractorExtractedVideoFile.postValue(file);
  }

  public LiveData<File> getExtractorExtractedMetadataFile() {
    return extractorExtractedMetadataFile;
  }

  public void setExtractorExtractedMetadataFile(File file) {
    extractorExtractedMetadataFile.postValue(file);
  }

  public LiveData<File> getBuiltMotionPhotoFile() {
    return builtMotionPhotoFile;
  }

  public void setBuiltMotionPhotoFile(File file) {
    builtMotionPhotoFile.postValue(file);
  }
}
