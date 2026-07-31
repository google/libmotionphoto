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
import android.widget.TextView;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import androidx.media3.common.MediaItem;
import androidx.media3.common.Player;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.ui.PlayerView;
import java.io.File;

/** Fragment for selecting the primary video in Creator flow using ExoPlayer for preview. */
@android.annotation.SuppressLint({"NewApi", "SetTextI18n"})
public class CreatorVideoFragment extends Fragment {
  private PlaygroundViewModel viewModel;
  private TextView tvVideoPath;
  private PlayerView pvVideoPreview;
  private ExoPlayer exoPlayer;
  private Button btnNextToBuild;

  private final ActivityResultLauncher<String> pickVideoLauncher =
      registerForActivityResult(
          new ActivityResultContracts.GetContent(),
          uri -> {
            if (uri != null) {
              viewModel.setVideoUri(uri);
            }
          });

  @Override
  public View onCreateView(
      LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    View view = inflater.inflate(R.layout.fragment_creator_video, container, false);
    viewModel = new ViewModelProvider(requireActivity()).get(PlaygroundViewModel.class);

    tvVideoPath = view.findViewById(R.id.tv_video_path);
    pvVideoPreview = view.findViewById(R.id.pv_video_preview);
    btnNextToBuild = view.findViewById(R.id.btn_next_to_build);

    initializePlayer();

    view.findViewById(R.id.btn_select_video)
        .setOnClickListener(v -> pickVideoLauncher.launch("video/*"));

    view.findViewById(R.id.btn_default_video).setOnClickListener(v -> useDefaultVideo());

    view.findViewById(R.id.btn_back_to_image)
        .setOnClickListener(v -> ((MainActivity) requireActivity()).navigateToCreatorImage());

    view.findViewById(R.id.btn_creator_video_main_menu)
        .setOnClickListener(v -> ((MainActivity) requireActivity()).navigateToTriage());

    btnNextToBuild.setOnClickListener(
        v -> ((MainActivity) requireActivity()).navigateToCreatorBuild());

    // Observe changes
    viewModel
        .getVideoUri()
        .observe(
            getViewLifecycleOwner(),
            uri -> {
              if (uri != null) {
                tvVideoPath.setText(StorageUtils.getFileNameFromUri(getContext(), uri));
                playVideoUri(uri);
                btnNextToBuild.setEnabled(true);
              } else {
                tvVideoPath.setText("No video selected");
                if (exoPlayer != null) {
                  exoPlayer.stop();
                  exoPlayer.clearMediaItems();
                }
                btnNextToBuild.setEnabled(false);
              }
            });

    return view;
  }

  private void initializePlayer() {
    if (exoPlayer == null) {
      exoPlayer = new ExoPlayer.Builder(requireContext()).build();
      exoPlayer.setRepeatMode(Player.REPEAT_MODE_ALL);
      pvVideoPreview.setPlayer(exoPlayer);
    }
  }

  private void playVideoUri(Uri uri) {
    if (exoPlayer == null) {
      initializePlayer();
    }
    MediaItem mediaItem = MediaItem.fromUri(uri);
    exoPlayer.setMediaItem(mediaItem);
    exoPlayer.prepare();
    exoPlayer.play();
  }

  @Override
  public void onPause() {
    super.onPause();
    if (exoPlayer != null) {
      exoPlayer.pause();
    }
  }

  @Override
  public void onDestroyView() {
    super.onDestroyView();
    if (exoPlayer != null) {
      exoPlayer.release();
      exoPlayer = null;
    }
  }

  private void useDefaultVideo() {
    File defaultVideo =
        ((MainActivity) requireActivity()).copyAssetToCache("motion_photo_video_only.mp4");
    if (defaultVideo != null) {
      viewModel.setVideoUri(Uri.fromFile(defaultVideo));
    }
  }
}
