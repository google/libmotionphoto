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
import androidx.appcompat.widget.SwitchCompat;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import java.io.File;

/** Fragment for selecting the photo to extract in Extractor flow. */
@android.annotation.SuppressLint({"NewApi", "SetTextI18n"})
public class ExtractorSelectFragment extends Fragment {
  private PlaygroundViewModel viewModel;
  private TextView tvExtractPhotoPath;
  private ImageView ivExtractPhotoPreview;
  private Button btnStartExtract;

  private final ActivityResultLauncher<String> pickExtractPhotoLauncher =
      registerForActivityResult(
          new ActivityResultContracts.GetContent(),
          uri -> {
            if (uri != null) {
              clearPreviousExtraction();
              viewModel.setExtractPhotoUri(uri);
            }
          });

  @Override
  public View onCreateView(
      LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    View view = inflater.inflate(R.layout.fragment_extractor_select, container, false);
    viewModel = new ViewModelProvider(requireActivity()).get(PlaygroundViewModel.class);

    tvExtractPhotoPath = view.findViewById(R.id.tv_extract_photo_path);
    ivExtractPhotoPreview = view.findViewById(R.id.iv_extract_photo_preview);
    btnStartExtract = view.findViewById(R.id.btn_start_extract);

    SwitchCompat swDisable3p = view.findViewById(R.id.sw_disable_3p_plugins);
    swDisable3p.setChecked(Boolean.TRUE.equals(viewModel.getDisable3pPlugins().getValue()));
    swDisable3p.setOnCheckedChangeListener(
        (buttonView, isChecked) -> viewModel.setDisable3pPlugins(isChecked));

    view.findViewById(R.id.btn_select_extract_photo)
        .setOnClickListener(v -> pickExtractPhotoLauncher.launch("image/*"));

    view.findViewById(R.id.btn_default_extract_photo)
        .setOnClickListener(v -> useDefaultExtractPhoto());

    view.findViewById(R.id.btn_extract_back_to_triage)
        .setOnClickListener(v -> ((MainActivity) requireActivity()).navigateToTriage());

    btnStartExtract.setOnClickListener(
        v -> {
          if (viewModel.getExtractPhotoUri().getValue() != null) {
            viewModel.setExtractorExtractedImageFile(null);
            viewModel.setExtractorExtractedVideoFile(null);
            viewModel.setExtractorExtractedMetadataFile(null);
            viewModel.setExtractorStatus("Extracting...");
            viewModel.clearExtractorLogs();
            ((MainActivity) requireActivity()).navigateToExtractorResult();
          }
        });

    // Observe changes
    viewModel
        .getExtractPhotoUri()
        .observe(
            getViewLifecycleOwner(),
            uri -> {
              if (uri != null) {
                if (getContext() != null) {
                  try (android.database.Cursor cursor =
                      getContext().getContentResolver().query(uri, null, null, null, null)) {
                    if (cursor != null && cursor.moveToFirst()) {
                      for (String col : cursor.getColumnNames()) {
                        android.util.Log.d(
                            "MP_URI_DEBUG",
                            col + " = " + cursor.getString(cursor.getColumnIndexOrThrow(col)));
                      }
                    }
                  } catch (Exception e) {
                    android.util.Log.e("MP_URI_DEBUG", "Error querying cursor", e);
                  }
                }
                tvExtractPhotoPath.setText(StorageUtils.getFileNameFromUri(getContext(), uri));
                ivExtractPhotoPreview.setImageURI(uri);
                btnStartExtract.setEnabled(true);
              } else {
                tvExtractPhotoPath.setText("No photo selected");
                ivExtractPhotoPreview.setImageDrawable(null);
                btnStartExtract.setEnabled(false);
              }
            });

    return view;
  }

  private void useDefaultExtractPhoto() {
    File generatedPhoto =
        new File(requireContext().getExternalFilesDir(null), "output_motion_photo.jpg");
    File defaultPhoto =
        (generatedPhoto.exists() && generatedPhoto.length() > 0)
            ? generatedPhoto
            : ((MainActivity) requireActivity()).copyAssetToCache("motion_photo.MP.jpg");
    if (defaultPhoto != null) {
      clearPreviousExtraction();
      viewModel.setExtractPhotoUri(Uri.fromFile(defaultPhoto));
    }
  }

  private void clearPreviousExtraction() {
    File oldImg = viewModel.getExtractorExtractedImageFile().getValue();
    if (oldImg != null && oldImg.exists()) {
      oldImg.delete();
    }
    File oldVid = viewModel.getExtractorExtractedVideoFile().getValue();
    if (oldVid != null && oldVid.exists()) {
      oldVid.delete();
    }
    File oldMeta = viewModel.getExtractorExtractedMetadataFile().getValue();
    if (oldMeta != null && oldMeta.exists()) {
      oldMeta.delete();
    }

    viewModel.setExtractorExtractedImageFile(null);
    viewModel.setExtractorExtractedVideoFile(null);
    viewModel.setExtractorExtractedMetadataFile(null);
    viewModel.clearExtractorLogs();
    viewModel.setExtractorStatus("Ready to extract");
  }
}
