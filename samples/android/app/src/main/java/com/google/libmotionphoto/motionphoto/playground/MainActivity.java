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

import android.content.pm.ActivityInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import androidx.appcompat.app.AppCompatActivity;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentTransaction;
import androidx.lifecycle.ViewModelProvider;
import java.io.File;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Main activity for the Motion Photo playground sample app. */
public class MainActivity extends AppCompatActivity implements NavigationListener {
  private static final String TAG = "MPPlaygroundMain";
  private PlaygroundViewModel viewModel;
  private final ExecutorService executor = Executors.newSingleThreadExecutor();

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      getWindow().setColorMode(ActivityInfo.COLOR_MODE_HDR);
    }
    setContentView(R.layout.activity_main);

    viewModel = new ViewModelProvider(this).get(PlaygroundViewModel.class);

    if (savedInstanceState == null) {
      navigateToTriage();
      prepareDefaultResources();
    }
  }

  @Override
  protected void onDestroy() {
    super.onDestroy();
    executor.shutdown();
  }

  private void prepareDefaultResources() {
    executor.execute(
        () -> {
          try {
            File defaultImage = StorageUtils.copyAssetToCache(this, "motion_photo_photo_only.jpg");
            File defaultVideo = StorageUtils.copyAssetToCache(this, "motion_photo_video_only.mp4");
            File defaultPhoto = StorageUtils.copyAssetToCache(this, "motion_photo.MP.jpg");
            if (defaultImage != null && defaultVideo != null && defaultPhoto != null) {
              viewModel.setImageUri(Uri.fromFile(defaultImage));
              viewModel.setVideoUri(Uri.fromFile(defaultVideo));
              viewModel.setExtractPhotoUri(Uri.fromFile(defaultPhoto));
              Log.d(TAG, "Loaded default resources from assets to ViewModel.");
            } else {
              Log.e(TAG, "Failed to load default resources.");
            }
          } catch (IOException e) {
            Log.e(TAG, "Error preparing defaults: " + e.getMessage());
          }
        });
  }

  /** Package-private wrapper for fragments to copy assets to cache. */
  File copyAssetToCache(String assetName) {
    try {
      return StorageUtils.copyAssetToCache(this, assetName);
    } catch (IOException e) {
      Log.e(TAG, "Error copying asset " + assetName + ": " + e.getMessage());
      return null;
    }
  }

  // Navigation Methods
  public void navigateToTriage() {
    replaceFragment(new TriageFragment(), "Triage", false);
  }

  public void navigateToCaptureDemo() {
    replaceFragment(new CaptureDemoFragment(), "CaptureDemo", true);
  }

  public void navigateToCreatorImage() {
    replaceFragment(new CreatorImageFragment(), "CreatorImage", true);
  }

  public void navigateToCreatorVideo() {
    replaceFragment(new CreatorVideoFragment(), "CreatorVideo", true);
  }

  public void navigateToCreatorBuild() {
    replaceFragment(new CreatorBuildFragment(), "CreatorBuild", true);
  }

  public void navigateToCreatorResult() {
    replaceFragment(new CreatorResultFragment(), "CreatorResult", true);
  }

  public void navigateToExtractorSelect() {
    replaceFragment(new ExtractorSelectFragment(), "ExtractorSelect", true);
  }

  public void navigateToExtractorResult() {
    replaceFragment(new ExtractorResultFragment(), "ExtractorResult", true);
  }

  private void replaceFragment(Fragment fragment, String tag, boolean addToBackStack) {
    FragmentTransaction transaction =
        getSupportFragmentManager()
            .beginTransaction()
            .replace(R.id.fragment_container, fragment, tag);
    if (addToBackStack) {
      transaction.addToBackStack(tag);
    }
    transaction.commit();
  }
}
