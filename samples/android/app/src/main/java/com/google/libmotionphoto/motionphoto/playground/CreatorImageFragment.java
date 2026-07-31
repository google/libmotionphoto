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
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.TextView;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import java.io.File;

/** Fragment for selecting the primary image in Creator flow. */
@android.annotation.SuppressLint({"NewApi", "SetTextI18n"})
public class CreatorImageFragment extends Fragment {
  private PlaygroundViewModel viewModel;
  private TextView tvImagePath;
  private ImageView ivImagePreview;
  private Button btnNextToVideo;

  private final ActivityResultLauncher<String> pickImageLauncher =
      registerForActivityResult(
          new ActivityResultContracts.GetContent(),
          uri -> {
            if (uri != null) {
              viewModel.setImageUri(uri);
            }
          });

  @Override
  public View onCreateView(
      LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    View view = inflater.inflate(R.layout.fragment_creator_image, container, false);
    viewModel = new ViewModelProvider(requireActivity()).get(PlaygroundViewModel.class);

    tvImagePath = view.findViewById(R.id.tv_image_path);
    ivImagePreview = view.findViewById(R.id.iv_image_preview);
    btnNextToVideo = view.findViewById(R.id.btn_next_to_video);

    view.findViewById(R.id.btn_select_image)
        .setOnClickListener(v -> pickImageLauncher.launch("image/*"));

    view.findViewById(R.id.btn_default_image).setOnClickListener(v -> useDefaultImage());

    view.findViewById(R.id.btn_back_to_triage)
        .setOnClickListener(v -> ((MainActivity) requireActivity()).navigateToTriage());

    btnNextToVideo.setOnClickListener(
        v -> ((MainActivity) requireActivity()).navigateToCreatorVideo());

    // Observe changes
    viewModel
        .getImageUri()
        .observe(
            getViewLifecycleOwner(),
            uri -> {
              if (uri != null) {
                tvImagePath.setText(StorageUtils.getFileNameFromUri(getContext(), uri));
                ivImagePreview.setImageURI(uri);
                btnNextToVideo.setEnabled(true);
              } else {
                tvImagePath.setText("No image selected");
                ivImagePreview.setImageDrawable(null);
                btnNextToVideo.setEnabled(false);
              }
            });

    return view;
  }

  private void useDefaultImage() {
    File defaultImage =
        ((MainActivity) requireActivity()).copyAssetToCache("motion_photo_photo_only.jpg");
    if (defaultImage != null) {
      viewModel.setImageUri(Uri.fromFile(defaultImage));
    }
  }
}
