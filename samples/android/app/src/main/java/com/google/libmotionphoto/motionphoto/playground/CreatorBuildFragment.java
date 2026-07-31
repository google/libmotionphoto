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

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import java.io.File;

/** Fragment for configuring and triggering the Motion Photo build in Creator flow. */
public class CreatorBuildFragment extends Fragment {
  private PlaygroundViewModel viewModel;
  private EditText etTimestamp;
  private Button btnBuild;

  @Override
  public View onCreateView(
      LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    View view = inflater.inflate(R.layout.fragment_creator_build, container, false);
    viewModel = new ViewModelProvider(requireActivity()).get(PlaygroundViewModel.class);

    etTimestamp = view.findViewById(R.id.et_timestamp);
    btnBuild = view.findViewById(R.id.btn_build);

    btnBuild.setOnClickListener(v -> buildMotionPhoto());
    view.findViewById(R.id.btn_back_to_video)
        .setOnClickListener(v -> ((MainActivity) requireActivity()).navigateToCreatorVideo());
    view.findViewById(R.id.btn_creator_build_main_menu)
        .setOnClickListener(v -> ((MainActivity) requireActivity()).navigateToTriage());

    // Enable build only if we have both image and video
    viewModel.getImageUri().observe(getViewLifecycleOwner(), img -> checkReady());
    viewModel.getVideoUri().observe(getViewLifecycleOwner(), vid -> checkReady());

    return view;
  }

  private void checkReady() {
    btnBuild.setEnabled(
        viewModel.getImageUri().getValue() != null && viewModel.getVideoUri().getValue() != null);
  }

  private void buildMotionPhoto() {
    viewModel.clearCreatorLogs();

    // Reset preview state in ViewModel
    File oldExtracted = viewModel.getExtractedVideoFile().getValue();
    if (oldExtracted != null && oldExtracted.exists()) {
      oldExtracted.delete();
    }
    viewModel.setExtractedVideoFile(null);
    viewModel.setBuiltMotionPhotoFile(null);

    final String timestampStr = etTimestamp.getText().toString();
    long timestampUs = 0L;
    try {
      if (!timestampStr.trim().isEmpty()) {
        timestampUs = Long.parseLong(timestampStr.trim());
      }
    } catch (NumberFormatException e) {
      viewModel.appendCreatorLog("Invalid timestamp: " + timestampStr + ", defaulting to 0");
    }
    viewModel.setTimestampUs(timestampUs);

    // Navigate to results page immediately to show "Building..." placeholder and run build
    ((MainActivity) requireActivity()).navigateToCreatorResult();
  }
}
