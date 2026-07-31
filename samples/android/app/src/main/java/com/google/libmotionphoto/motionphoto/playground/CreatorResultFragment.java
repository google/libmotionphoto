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

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.graphics.Bitmap;
import android.graphics.ImageDecoder;
import android.graphics.PixelFormat;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.view.LayoutInflater;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.MimeTypeMap;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.ScrollView;
import android.widget.TextView;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContract;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import androidx.media3.common.MediaItem;
import androidx.media3.common.Player;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.ui.PlayerView;
import com.google.android.material.tabs.TabLayout;
import com.google.libmotionphoto.MotionPhotoBuilderService;
import com.google.libmotionphoto.MotionPhotoExtractorService;
import com.google.libmotionphoto.IMotionPhotoBuilderService;
import com.google.libmotionphoto.IMotionPhotoExtractorService;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/** Fragment for displaying the Motion Photo creation results using ExoPlayer for preview. */
public class CreatorResultFragment extends Fragment {
  private File pendingFileToSave;
  private final ExecutorService executor = Executors.newSingleThreadExecutor();

  private IMotionPhotoBuilderService builderService;
  private volatile boolean isBuilderBound = false;

  private IMotionPhotoExtractorService extractorService;
  private volatile boolean isExtractorBound = false;

  private final AtomicBoolean isBuilding = new AtomicBoolean(false);

  private final ServiceConnection builderConnection =
      new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
          builderService = IMotionPhotoBuilderService.Stub.asInterface(service);
          isBuilderBound = true;
          viewModel.appendCreatorLog("Bound to MotionPhotoBuilderService.");
          Activity activity = getActivity();
          if (activity != null) {
            activity.runOnUiThread(() -> checkAndStartBuild());
          }
        }

        @Override
        public void onServiceDisconnected(ComponentName arg0) {
          builderService = null;
          isBuilderBound = false;
          viewModel.appendCreatorLog("Disconnected from MotionPhotoBuilderService.");
        }
      };

  private final ServiceConnection extractorConnection =
      new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
          extractorService = IMotionPhotoExtractorService.Stub.asInterface(service);
          isExtractorBound = true;
          viewModel.appendCreatorLog("Bound to MotionPhotoExtractorService.");
          Activity activity = getActivity();
          if (activity != null) {
            activity.runOnUiThread(() -> checkAndStartBuild());
          }
        }

        @Override
        public void onServiceDisconnected(ComponentName arg0) {
          extractorService = null;
          isExtractorBound = false;
          viewModel.appendCreatorLog("Disconnected from MotionPhotoExtractorService.");
        }
      };

  @Override
  public void onStart() {
    super.onStart();
    Context context = getContext();
    if (context != null) {
      Intent builderIntent = new Intent(context, MotionPhotoBuilderService.class);
      context.bindService(builderIntent, builderConnection, Context.BIND_AUTO_CREATE);

      Intent extractorIntent = new Intent(context, MotionPhotoExtractorService.class);
      context.bindService(extractorIntent, extractorConnection, Context.BIND_AUTO_CREATE);
    }
  }

  @Override
  public void onStop() {
    super.onStop();
    Context context = getContext();
    if (context != null) {
      if (isBuilderBound) {
        context.unbindService(builderConnection);
        isBuilderBound = false;
      }
      if (isExtractorBound) {
        context.unbindService(extractorConnection);
        isExtractorBound = false;
      }
    }
  }

  private synchronized void checkAndStartBuild() {
    if (isBuilderBound
        && isExtractorBound
        && viewModel.getBuiltMotionPhotoFile().getValue() == null
        && viewModel.getExtractedVideoFile().getValue() == null) {
      buildMotionPhoto();
    }
  }

  private static class CreateDocumentDynamic
      extends ActivityResultContract<CreateDocumentDynamic.Params, Uri> {
    public static class Params {
      final String mimeType;
      final String title;

      public Params(String mimeType, String title) {
        this.mimeType = mimeType;
        this.title = title;
      }
    }

    @NonNull
    @Override
    public Intent createIntent(@NonNull Context context, Params input) {
      return new Intent(Intent.ACTION_CREATE_DOCUMENT)
          .addCategory(Intent.CATEGORY_OPENABLE)
          .setType(input.mimeType)
          .putExtra(Intent.EXTRA_TITLE, input.title);
    }

    @Override
    public Uri parseResult(int resultCode, @Nullable Intent intent) {
      if (intent == null || resultCode != Activity.RESULT_OK) {
        return null;
      }
      return intent.getData();
    }
  }

  private final ActivityResultLauncher<CreateDocumentDynamic.Params> saveLauncher =
      registerForActivityResult(
          new CreateDocumentDynamic(),
          uri -> {
            if (uri != null) {
              savePendingFile(uri);
            }
          });

  private PlaygroundViewModel viewModel;
  private TabLayout tabLayout;
  private View llResultPreview;
  private ImageView ivResultImage;
  private PlayerView pvResultVideo;
  private ExoPlayer exoPlayer;
  private Button btnPlayResult;
  private Button btnPauseResult;
  private Button btnShowStill;
  private boolean isPaused = false;
  private TextView tvPreviewPlaceholder;
  private ScrollView svLogs;
  private TextView tvLog;

  @Override
  public View onCreateView(
      LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    View view = inflater.inflate(R.layout.fragment_creator_result, container, false);
    viewModel = new ViewModelProvider(requireActivity()).get(PlaygroundViewModel.class);

    tabLayout = view.findViewById(R.id.tab_layout);
    llResultPreview = view.findViewById(R.id.ll_result_preview);
    ivResultImage = view.findViewById(R.id.iv_result_image);
    pvResultVideo = view.findViewById(R.id.pv_result_video);
    btnPlayResult = view.findViewById(R.id.btn_play_result);
    btnPauseResult = view.findViewById(R.id.btn_pause_result);
    btnShowStill = view.findViewById(R.id.btn_show_still);
    tvPreviewPlaceholder = view.findViewById(R.id.tv_preview_placeholder);
    svLogs = view.findViewById(R.id.sv_logs);
    tvLog = view.findViewById(R.id.tv_log);

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && getActivity() != null) {
      getActivity().getWindow().setColorMode(android.content.pm.ActivityInfo.COLOR_MODE_HDR);
    }

    initializePlayer();

    tabLayout.addOnTabSelectedListener(
        new TabLayout.OnTabSelectedListener() {
          @Override
          public void onTabSelected(TabLayout.Tab tab) {
            updateTabContent(tab.getPosition());
          }

          @Override
          public void onTabUnselected(TabLayout.Tab tab) {}

          @Override
          public void onTabReselected(TabLayout.Tab tab) {}
        });

    btnPlayResult.setOnClickListener(
        v -> {
          File extractedVideoFile = viewModel.getExtractedVideoFile().getValue();
          if (extractedVideoFile != null && extractedVideoFile.exists()) {
            btnPlayResult.setVisibility(View.GONE);
            btnPauseResult.setVisibility(View.VISIBLE);
            btnShowStill.setVisibility(View.VISIBLE);
            if (isPaused && exoPlayer != null) {
              ivResultImage.setVisibility(View.GONE);
              exoPlayer.play();
            } else {
              initializePlayer();
              exoPlayer.setMediaItem(MediaItem.fromUri(Uri.fromFile(extractedVideoFile)));
              exoPlayer.prepare();
              exoPlayer.play();
            }
            isPaused = false;
          }
        });

    btnPauseResult.setOnClickListener(
        v -> {
          if (exoPlayer != null && exoPlayer.isPlaying()) {
            exoPlayer.pause();
            isPaused = true;
            btnPauseResult.setVisibility(View.GONE);
            btnPlayResult.setVisibility(View.VISIBLE);
          }
        });

    btnShowStill.setOnClickListener(
        v -> {
          if (exoPlayer != null) {
            exoPlayer.stop();
          }
          ivResultImage.setVisibility(View.VISIBLE);
          btnPlayResult.setVisibility(View.VISIBLE);
          btnPauseResult.setVisibility(View.GONE);
          btnShowStill.setVisibility(View.GONE);
          isPaused = false;
        });

    view.findViewById(R.id.btn_creator_download)
        .setOnClickListener(
            v -> {
              File builtFile = viewModel.getBuiltMotionPhotoFile().getValue();
              if (builtFile != null && builtFile.exists()) {
                pendingFileToSave = builtFile;
                String mimeType = getMimeType(builtFile);
                String ext = builtFile.getName().substring(builtFile.getName().lastIndexOf('.'));
                saveLauncher.launch(
                    new CreateDocumentDynamic.Params(mimeType, "motion_photo" + ext));
              }
            });

    view.findViewById(R.id.btn_back_to_capture)
        .setOnClickListener(
            v -> {
              if (exoPlayer != null) {
                exoPlayer.stop();
              }
              ((MainActivity) requireActivity()).navigateToCaptureDemo();
            });

    view.findViewById(R.id.btn_back_to_build)
        .setOnClickListener(
            v -> {
              if (exoPlayer != null) {
                exoPlayer.stop();
              }
              ((MainActivity) requireActivity()).navigateToCreatorBuild();
            });

    // Observe changes
    viewModel
        .getCreatorLogs()
        .observe(
            getViewLifecycleOwner(),
            logs -> {
              tvLog.setText(logs);
              svLogs.post(() -> svLogs.fullScroll(View.FOCUS_DOWN));
            });

    viewModel
        .getExtractedVideoFile()
        .observe(
            getViewLifecycleOwner(),
            file -> {
              updateTabContent(tabLayout.getSelectedTabPosition());
            });

    viewModel
        .getImageUri()
        .observe(
            getViewLifecycleOwner(),
            uri -> {
              if (uri != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                  try {
                    ImageDecoder.Source source =
                        ImageDecoder.createSource(requireContext().getContentResolver(), uri);
                    Bitmap bitmap =
                        ImageDecoder.decodeBitmap(
                            source,
                            (decoder, info, src) ->
                                decoder.setAllocator(ImageDecoder.ALLOCATOR_SOFTWARE));
                    ivResultImage.setImageBitmap(bitmap);
                  } catch (IOException e) {
                    ivResultImage.setImageURI(uri);
                  }
                } else {
                  ivResultImage.setImageURI(uri);
                }
              }
            });

    viewModel
        .getBuiltMotionPhotoFile()
        .observe(
            getViewLifecycleOwner(),
            file -> {
              updateDownloadButtonState();
            });

    // Reset tab to Preview on creation
    tabLayout.selectTab(tabLayout.getTabAt(0));
    updateTabContent(0);
    checkAndStartBuild();

    return view;
  }

  private void initializePlayer() {
    if (exoPlayer == null) {
      exoPlayer = new ExoPlayer.Builder(requireContext()).build();
      exoPlayer.setRepeatMode(Player.REPEAT_MODE_OFF);
      pvResultVideo.setPlayer(exoPlayer);

      View videoSurface = pvResultVideo.getVideoSurfaceView();
      if (videoSurface instanceof SurfaceView && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
        ((SurfaceView) videoSurface).getHolder().setFormat(PixelFormat.RGBA_1010102);
      }

      exoPlayer.addListener(
          new Player.Listener() {
            @Override
            public void onRenderedFirstFrame() {
              ivResultImage.setVisibility(View.GONE);
            }

            @Override
            public void onPlaybackStateChanged(int playbackState) {
              if (playbackState == Player.STATE_ENDED) {
                ivResultImage.setVisibility(View.VISIBLE);
                btnPlayResult.setVisibility(View.VISIBLE);
                btnPauseResult.setVisibility(View.GONE);
                btnShowStill.setVisibility(View.GONE);
                isPaused = false;
              }
            }
          });
    }
  }

  @Override
  public void onPause() {
    super.onPause();
    if (exoPlayer != null && exoPlayer.isPlaying()) {
      exoPlayer.pause();
      isPaused = true;
      if (btnPlayResult != null) {
        btnPlayResult.setVisibility(View.VISIBLE);
      }
      if (btnPauseResult != null) {
        btnPauseResult.setVisibility(View.GONE);
      }
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

  @Override
  public void onDestroy() {
    super.onDestroy();
    executor.shutdown();
  }

  private void updateTabContent(int position) {
    if (position == 0) { // Preview
      svLogs.setVisibility(View.GONE);
      File extractedVideoFile = viewModel.getExtractedVideoFile().getValue();
      if (extractedVideoFile != null
          && extractedVideoFile.exists()
          && viewModel.getImageUri().getValue() != null) {
        llResultPreview.setVisibility(View.VISIBLE);
        tvPreviewPlaceholder.setVisibility(View.GONE);
      } else {
        llResultPreview.setVisibility(View.GONE);
        tvPreviewPlaceholder.setVisibility(View.VISIBLE);
      }
    } else if (position == 1) { // Logs
      svLogs.setVisibility(View.VISIBLE);
      llResultPreview.setVisibility(View.GONE);
      tvPreviewPlaceholder.setVisibility(View.GONE);
    }
    updateDownloadButtonState();
  }

  private String getMimeType(File file) {
    String ext = MimeTypeMap.getFileExtensionFromUrl(file.getAbsolutePath());
    if (ext == null || ext.isEmpty()) {
      String name = file.getName();
      int lastDot = name.lastIndexOf('.');
      if (lastDot != -1) {
        ext = name.substring(lastDot + 1);
      }
    }
    if (ext != null) {
      return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext.toLowerCase(Locale.US));
    }
    return "*/*";
  }

  @SuppressWarnings("ContentResolverUri")
  private void savePendingFile(Uri destinationUri) {
    if (pendingFileToSave == null || !pendingFileToSave.exists()) {
      viewModel.appendCreatorLog("No pending file to save or file doesn't exist.");
      return;
    }
    executor.execute(
        () -> {
          try {
            Context context = getContext();
            if (context != null) {
              OutputStream os = context.getContentResolver().openOutputStream(destinationUri);
              if (os != null) {
                StorageUtils.copyFileToStream(pendingFileToSave, os);
                viewModel.appendCreatorLog("File saved successfully.");
              } else {
                viewModel.appendCreatorLog("Failed to open output stream for destination.");
              }
            }
          } catch (IOException e) {
            viewModel.appendCreatorLog("Failed to save file: " + e.getMessage());
          }
        });
  }

  private void updateDownloadButtonState() {
    View btnDownload = getView() != null ? getView().findViewById(R.id.btn_creator_download) : null;
    if (btnDownload == null) {
      return;
    }
    int mainTabPos = tabLayout.getSelectedTabPosition();
    if (mainTabPos == 1) { // Logs
      btnDownload.setVisibility(View.GONE);
      return;
    }
    // Preview tab
    File builtFile = viewModel.getBuiltMotionPhotoFile().getValue();
    if (builtFile != null && builtFile.exists()) {
      btnDownload.setVisibility(View.VISIBLE);
    } else {
      btnDownload.setVisibility(View.GONE);
    }
  }

  private void buildMotionPhoto() {
    if (!isBuilding.compareAndSet(false, true)) {
      return;
    }
    viewModel.clearCreatorLogs();
    viewModel.appendCreatorLog("Starting build process...");

    final Context context = getContext();
    if (context == null) {
      isBuilding.set(false);
      return;
    }

    final Uri imageUri = viewModel.getImageUri().getValue();
    final Uri videoUri = viewModel.getVideoUri().getValue();
    if (imageUri == null || videoUri == null) {
      viewModel.appendCreatorLog("Error: Missing input image or video URI.");
      isBuilding.set(false);
      return;
    }

    final long timestampUs =
        viewModel.getTimestampUs().getValue() != null ? viewModel.getTimestampUs().getValue() : 0L;

    executor.execute(
        () -> {
          try {
            int waitMs = 0;
            while ((!isBuilderBound
                    || builderService == null
                    || !isExtractorBound
                    || extractorService == null)
                && waitMs < 5000) {
              try {
                Thread.sleep(50);
              } catch (InterruptedException ignored) {
                // Polling wait loop for service connection.
              }
              waitMs += 50;
            }

            viewModel.appendCreatorLog("Copying input files to temp directory...");
            String imagePath =
                StorageUtils.copyUriToTempFile(context, imageUri, "input_image", ".jpg");
            String videoPath =
                StorageUtils.copyUriToTempFile(context, videoUri, "input_video", ".mp4");

            if (imagePath == null || videoPath == null) {
              viewModel.appendCreatorLog("Failed to copy input files.");
              return;
            }

            viewModel.appendCreatorLog("Generating XMP metadata...");
            String xmpPath = StorageUtils.generateXmpFile(context, timestampUs);
            if (xmpPath == null) {
              viewModel.appendCreatorLog("Failed to generate XMP metadata.");
              return;
            }

            String ext = imagePath.substring(imagePath.lastIndexOf('.'));
            File outputFile =
                new File(context.getExternalFilesDir(null), "output_motion_photo" + ext);
            viewModel.appendCreatorLog("Output file will be: " + outputFile.getAbsolutePath());

            viewModel.appendCreatorLog("Calling Sandboxed MotionPhotoBuilderService...");
            int result = -1;
            if (isBuilderBound && builderService != null) {
              result =
                  builderService.buildMotionPhoto(
                      imagePath,
                      videoPath,
                      null,
                      xmpPath,
                      timestampUs,
                      outputFile.getAbsolutePath());
            } else {
              viewModel.appendCreatorLog("Builder service not bound, cannot build motion photo.");
            }

            viewModel.appendCreatorLog("Builder returned: " + result);
            if (result == 0) {
              viewModel.appendCreatorLog(
                  "SUCCESS! Motion photo created at " + outputFile.getAbsolutePath());
              viewModel.setBuiltMotionPhotoFile(outputFile);
              prepareResultPreview(context, outputFile);
            } else {
              viewModel.appendCreatorLog("FAILED! Check logs above.");
            }

            // Clean up temp files
            new File(imagePath).delete();
            new File(videoPath).delete();
            new File(xmpPath).delete();

          } catch (IOException e) {
            viewModel.appendCreatorLog("IOException: " + e.getMessage());
          } catch (Exception e) {
            viewModel.appendCreatorLog("Exception: " + e.getMessage());
          } finally {
            isBuilding.set(false);
          }
        });
  }

  private void prepareResultPreview(Context context, File motionPhotoFile) {
    viewModel.appendCreatorLog("Preparing result preview...");

    File extractedVideoFile;
    try {
      extractedVideoFile = File.createTempFile("extracted_result", ".mp4", context.getCacheDir());
    } catch (IOException e) {
      viewModel.appendCreatorLog("Failed to create temp file for preview: " + e.getMessage());
      return;
    }

    String ext = motionPhotoFile.getName().substring(motionPhotoFile.getName().lastIndexOf('.'));
    File tempImageFile = new File(context.getCacheDir(), "extracted_temp_image" + ext);
    File tempMetadataFile = new File(context.getCacheDir(), "extracted_temp_metadata.xml");

    viewModel.appendCreatorLog("Extracting video for playback via MotionPhotoExtractorService...");
    int extractResult = -1;
    if (isExtractorBound && extractorService != null) {
      try {
        extractResult =
            extractorService.extractMotionPhoto(
                motionPhotoFile.getAbsolutePath(),
                tempImageFile.getAbsolutePath(),
                extractedVideoFile.getAbsolutePath(),
                tempMetadataFile.getAbsolutePath());
      } catch (Exception e) {
        viewModel.appendCreatorLog("Error during preview extraction: " + e.getMessage());
      }
    } else {
      viewModel.appendCreatorLog("Extractor service not bound for preview extraction.");
    }
    viewModel.appendCreatorLog("Extractor returned: " + extractResult);
    if (extractResult == 0) {
      viewModel.appendCreatorLog("Result video extracted. Ready to play.");
      viewModel.setExtractedVideoFile(extractedVideoFile);
    } else {
      viewModel.appendCreatorLog("Failed to extract result video.");
      extractedVideoFile.delete();
    }
    tempImageFile.delete();
    tempMetadataFile.delete();
  }
}
