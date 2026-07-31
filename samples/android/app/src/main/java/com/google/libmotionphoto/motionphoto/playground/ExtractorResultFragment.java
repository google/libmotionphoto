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
import android.content.pm.ActivityInfo;
import android.graphics.Bitmap;
import android.graphics.ImageDecoder;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.util.Log;
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
import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import androidx.media3.common.MediaItem;
import androidx.media3.common.Player;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.ui.PlayerView;
import com.google.android.material.tabs.TabLayout;
import com.google.libmotionphoto.MetadataParserService;
import com.google.libmotionphoto.MotionPhotoExtractorService;
import com.google.libmotionphoto.IMetadataParserService;
import com.google.libmotionphoto.IMotionPhotoExtractorService;
import com.google.libmotionphoto.motionphoto.proto.MetadataBlock;
import com.google.libmotionphoto.motionphoto.proto.MetadataCollection;
import com.google.libmotionphoto.motionphoto.proto.MotionPhotoMetadata;
import com.google.protobuf.ExtensionRegistryLite;
import com.google.protobuf.InvalidProtocolBufferException;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.json.JSONObject;

/** Fragment for displaying the Motion Photo extraction results, metadata, and logs. */
@android.annotation.SuppressLint({"NewApi", "SetTextI18n"})
public class ExtractorResultFragment extends Fragment {
  private File pendingFileToSave;
  private final ExecutorService executor = Executors.newSingleThreadExecutor();

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
  private TabLayout extractorTabLayout;
  private View llExtractorResultPreview;
  private TabLayout extractorPreviewSubTabLayout;
  private ImageView ivExtractorResultImage;
  private View flExtractorResultVideoContainer;
  private PlayerView pvExtractorResultVideo;
  private ExoPlayer videoPlayer;
  private ScrollView svExtractorResultMetadata;
  private ViewGroup clMetadataBlocksContainer;
  private TextView tvExtractorPreviewPlaceholder;
  private ScrollView svExtractorLogs;
  private TextView tvExtractorLog;
  private ViewGroup llExtractorResultCompare;
  private View flCompareOverlayContainer;
  private View flCompareVideoContainer;
  private PlayerView pvCompareVideo;
  private ExoPlayer comparePlayer;
  private ImageView ivCompareImage;
  private View vCompareDivider;
  private com.google.android.material.slider.Slider sliderCompareSplit;
  private TextView tvCompareStillInfo;
  private TextView tvCompareVideoInfo;
  private IMetadataParserService metadataParserService;
  private volatile boolean isParserBound = false;
  private IMotionPhotoExtractorService extractorService;
  private volatile boolean isExtractorBound = false;

  private final java.util.concurrent.atomic.AtomicBoolean isExtracting =
      new java.util.concurrent.atomic.AtomicBoolean(false);
  private String extractedAgtmJson = null;

  private synchronized void checkAndStartExtraction() {
    if (isParserBound
        && isExtractorBound
        && viewModel.getExtractorExtractedImageFile().getValue() == null) {
      extractMotionPhoto();
    }
  }

  private final ServiceConnection parserConnection =
      new ServiceConnection() {

        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
          metadataParserService = IMetadataParserService.Stub.asInterface(service);
          isParserBound = true;
          viewModel.appendExtractorLog("Bound to MetadataParserService.");
          Activity activity = getActivity();
          if (activity != null) {

            activity.runOnUiThread(
                () -> {
                  parseAndDisplayMetadata();
                  checkAndStartExtraction();
                });
          }
        }

        @Override
        public void onServiceDisconnected(ComponentName arg0) {
          metadataParserService = null;
          isParserBound = false;
          viewModel.appendExtractorLog("Disconnected from MetadataParserService.");
        }
      };

  private final ServiceConnection extractorConnection =
      new ServiceConnection() {

        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
          extractorService = IMotionPhotoExtractorService.Stub.asInterface(service);
          isExtractorBound = true;
          viewModel.appendExtractorLog("Bound to MotionPhotoExtractorService.");
          Activity activity = getActivity();
          if (activity != null) {

            activity.runOnUiThread(
                () -> {
                  checkAndStartExtraction();
                });
          }
        }

        @Override
        public void onServiceDisconnected(ComponentName arg0) {
          extractorService = null;
          isExtractorBound = false;
          viewModel.appendExtractorLog("Disconnected from MotionPhotoExtractorService.");
        }
      };

  @Override
  public View onCreateView(
      LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    View view = inflater.inflate(R.layout.fragment_extractor_result, container, false);
    viewModel = new ViewModelProvider(requireActivity()).get(PlaygroundViewModel.class);
    extractorTabLayout = view.findViewById(R.id.extractor_tab_layout);
    llExtractorResultPreview = view.findViewById(R.id.ll_extractor_result_preview);
    extractorPreviewSubTabLayout = view.findViewById(R.id.extractor_preview_sub_tab_layout);
    ivExtractorResultImage = view.findViewById(R.id.iv_extractor_result_image);
    flExtractorResultVideoContainer = view.findViewById(R.id.fl_extractor_result_video_container);
    pvExtractorResultVideo = view.findViewById(R.id.pv_extractor_result_video);
    svExtractorResultMetadata = view.findViewById(R.id.sv_extractor_result_metadata);
    clMetadataBlocksContainer = view.findViewById(R.id.cl_metadata_blocks_container);
    tvExtractorPreviewPlaceholder = view.findViewById(R.id.tv_extractor_preview_placeholder);
    svExtractorLogs = view.findViewById(R.id.sv_extractor_logs);
    tvExtractorLog = view.findViewById(R.id.tv_extractor_log);
    llExtractorResultCompare = view.findViewById(R.id.ll_extractor_result_compare);
    flCompareOverlayContainer = view.findViewById(R.id.fl_compare_overlay_container);
    flCompareVideoContainer = view.findViewById(R.id.fl_compare_video_container);
    pvCompareVideo = view.findViewById(R.id.pv_compare_video);
    ivCompareImage = view.findViewById(R.id.iv_compare_image);
    vCompareDivider = view.findViewById(R.id.v_compare_divider);
    sliderCompareSplit = view.findViewById(R.id.slider_compare_split);
    tvCompareStillInfo = view.findViewById(R.id.tv_compare_still_info);
    tvCompareVideoInfo = view.findViewById(R.id.tv_compare_video_info);
    initializeVideoPlayer();
    initializeComparePlayer();
    sliderCompareSplit.addOnChangeListener((slider, value, fromUser) -> updateCompareSplit(value));

    flCompareOverlayContainer.addOnLayoutChangeListener(
        (v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
          updateCompareSplit(sliderCompareSplit.getValue());
        });

    extractorTabLayout.addOnTabSelectedListener(
        new TabLayout.OnTabSelectedListener() {

          @Override
          public void onTabSelected(TabLayout.Tab tab) {
            updateExtractorTabContent(tab.getPosition());
          }

          @Override
          public void onTabUnselected(TabLayout.Tab tab) {}

          @Override
          public void onTabReselected(TabLayout.Tab tab) {}
        });

    extractorPreviewSubTabLayout.addOnTabSelectedListener(
        new TabLayout.OnTabSelectedListener() {

          @Override
          public void onTabSelected(TabLayout.Tab tab) {
            updateExtractorPreviewSubTabContent(tab.getPosition());
          }

          @Override
          public void onTabUnselected(TabLayout.Tab tab) {}

          @Override
          public void onTabReselected(TabLayout.Tab tab) {}
        });
    view.findViewById(R.id.btn_extractor_download)
        .setOnClickListener(
            v -> {
              int subTabPos = extractorPreviewSubTabLayout.getSelectedTabPosition();
              if (subTabPos == 0) {
                File imgFile = viewModel.getExtractorExtractedImageFile().getValue();
                if (imgFile != null && imgFile.exists()) {
                  pendingFileToSave = imgFile;
                  String mimeType = getMimeType(imgFile);
                  String ext = imgFile.getName().substring(imgFile.getName().lastIndexOf('.'));

                  saveLauncher.launch(
                      new CreateDocumentDynamic.Params(mimeType, "extracted_image" + ext));
                }

              } else if (subTabPos == 1) {
                File vidFile = viewModel.getExtractorExtractedVideoFile().getValue();
                if (vidFile != null && vidFile.exists()) {
                  pendingFileToSave = vidFile;

                  saveLauncher.launch(
                      new CreateDocumentDynamic.Params("video/mp4", "extracted_video.mp4"));
                }
              }
            });
    view.findViewById(R.id.btn_extractor_back_to_select)
        .setOnClickListener(
            v -> {
              if (videoPlayer != null) {
                videoPlayer.stop();
              }

              if (comparePlayer != null) {
                comparePlayer.stop();
              }

              ((MainActivity) requireActivity()).navigateToExtractorSelect();
            });

    // Observe changes

    viewModel
        .getExtractorLogs()
        .observe(
            getViewLifecycleOwner(),
            logs -> {
              tvExtractorLog.setText(logs);
              svExtractorLogs.post(() -> svExtractorLogs.fullScroll(View.FOCUS_DOWN));
            });
    viewModel
        .getExtractorExtractedImageFile()
        .observe(
            getViewLifecycleOwner(),
            file -> {
              if (file != null && file.exists()) {
                boolean isHdr = loadImageFile(file);
                updateImageTabHdrState(isHdr);
                updateExtractorTabContent(extractorTabLayout.getSelectedTabPosition());
                updateDownloadButtonState();
              }
            });
    viewModel
        .getExtractorExtractedVideoFile()
        .observe(
            getViewLifecycleOwner(),
            file -> {
              if (file != null && file.exists()) {
                if (extractorTabLayout.getSelectedTabPosition() == 0
                    && extractorPreviewSubTabLayout.getSelectedTabPosition() == 1) {
                  initializeVideoPlayer();
                  videoPlayer.setMediaItem(MediaItem.fromUri(Uri.fromFile(file)));
                  videoPlayer.prepare();
                  videoPlayer.play();
                }

                updateExtractorTabContent(extractorTabLayout.getSelectedTabPosition());
                updateDownloadButtonState();
              }
            });
    viewModel
        .getExtractorExtractedMetadataFile()
        .observe(
            getViewLifecycleOwner(),
            file -> {
              parseAndDisplayMetadata();
            });
    viewModel
        .getExtractorStatus()
        .observe(
            getViewLifecycleOwner(),
            status -> {
              tvExtractorPreviewPlaceholder.setText(status);
            });

    // Reset tabs to default state on startup (Preview -> Image)
    extractorTabLayout.selectTab(extractorTabLayout.getTabAt(0));
    extractorPreviewSubTabLayout.selectTab(extractorPreviewSubTabLayout.getTabAt(0));
    updateExtractorTabContent(0);
    updateExtractorPreviewSubTabContent(0);
    return view;
  }

  private void initializeVideoPlayer() {
    if (videoPlayer == null) {
      videoPlayer = new ExoPlayer.Builder(requireContext()).build();
      pvExtractorResultVideo.setPlayer(videoPlayer);
      View surface = pvExtractorResultVideo.getVideoSurfaceView();
      if (surface instanceof SurfaceView && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
        ((SurfaceView) surface).getHolder().setFormat(PixelFormat.RGBA_1010102);
      }
    }
  }

  private void initializeComparePlayer() {
    if (comparePlayer == null) {
      comparePlayer = new ExoPlayer.Builder(requireContext()).build();
      comparePlayer.setRepeatMode(Player.REPEAT_MODE_ALL);
      pvCompareVideo.setPlayer(comparePlayer);
      View surface = pvCompareVideo.getVideoSurfaceView();
      if (surface instanceof SurfaceView && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
        ((SurfaceView) surface).getHolder().setFormat(PixelFormat.RGBA_1010102);
      }
    }
  }

  @Override
  public void onPause() {
    super.onPause();
    if (videoPlayer != null) {
      videoPlayer.pause();
    }

    if (comparePlayer != null) {
      comparePlayer.pause();
    }
  }

  @Override
  public void onDestroyView() {
    super.onDestroyView();
    if (videoPlayer != null) {
      videoPlayer.release();
      videoPlayer = null;
    }

    if (comparePlayer != null) {
      comparePlayer.release();
      comparePlayer = null;
    }
  }

  @Override
  public void onDestroy() {
    super.onDestroy();
    executor.shutdown();
  }

  private void updateCompareSplit(float percent) {
    int width = flCompareOverlayContainer.getWidth();
    int height = flCompareOverlayContainer.getHeight();
    if (width <= 0 || height <= 0) {
      return;
    }

    int splitX = (int) (width * (percent / 100.0f));
    ivCompareImage.setClipBounds(new Rect(0, 0, splitX, height));
    flCompareVideoContainer.setClipBounds(new Rect(splitX, 0, width, height));
    vCompareDivider.setTranslationX(splitX);
  }

  private void updateExtractorTabContent(int position) {
    if (position != 1 && comparePlayer != null) {
      comparePlayer.pause();
    }

    if (position == 0) { // Preview

      svExtractorLogs.setVisibility(View.GONE);
      llExtractorResultCompare.setVisibility(View.GONE);
      File extractedImg = viewModel.getExtractorExtractedImageFile().getValue();
      if (extractedImg != null && extractedImg.exists()) {
        llExtractorResultPreview.setVisibility(View.VISIBLE);
        tvExtractorPreviewPlaceholder.setVisibility(View.GONE);
      } else {
        llExtractorResultPreview.setVisibility(View.GONE);
        tvExtractorPreviewPlaceholder.setVisibility(View.VISIBLE);
      }

    } else if (position == 1) { // Compare

      svExtractorLogs.setVisibility(View.GONE);
      llExtractorResultPreview.setVisibility(View.GONE);
      tvExtractorPreviewPlaceholder.setVisibility(View.GONE);
      llExtractorResultCompare.setVisibility(View.VISIBLE);
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && getActivity() != null) {
        getActivity().getWindow().setColorMode(ActivityInfo.COLOR_MODE_HDR);
        try {

          java.lang.reflect.Method winMethod =
              android.view.Window.class.getMethod("setDesiredHdrHeadroom", float.class);
          winMethod.invoke(getActivity().getWindow(), 10.0f);
        } catch (Exception ignored) {

          // Ignored: setDesiredHdrHeadroom reflection is optional on unsupported platforms.

        }
      }

      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
        try {

          java.lang.reflect.Method viewMethod =
              View.class.getMethod("setDesiredHdrHeadroom", float.class);
          viewMethod.invoke(ivCompareImage, 10.0f);
          viewMethod.invoke(pvCompareVideo, 10.0f);
          viewMethod.invoke(flCompareOverlayContainer, 10.0f);
        } catch (Exception ignored) {

          // Ignored: setDesiredHdrHeadroom reflection is optional on unsupported platforms.

        }
      }

      boolean hasGainmap = false;
      File extractedImg = viewModel.getExtractorExtractedImageFile().getValue();
      if (extractedImg != null && extractedImg.exists()) {
        hasGainmap = loadImageFileIntoView(extractedImg, ivCompareImage);
      }

      File extractedVid = viewModel.getExtractorExtractedVideoFile().getValue();
      if (extractedVid != null && extractedVid.exists()) {
        initializeComparePlayer();
        comparePlayer.setMediaItem(MediaItem.fromUri(Uri.fromFile(extractedVid)));
        comparePlayer.prepare();
        comparePlayer.play();
      }

      updateCompareInfoCards(extractedImg, extractedVid);
      flCompareOverlayContainer.post(() -> updateCompareSplit(sliderCompareSplit.getValue()));
    } else if (position == 2) { // Logs

      svExtractorLogs.setVisibility(View.VISIBLE);
      llExtractorResultPreview.setVisibility(View.GONE);
      llExtractorResultCompare.setVisibility(View.GONE);
      tvExtractorPreviewPlaceholder.setVisibility(View.GONE);
    }

    updateDownloadButtonState();
  }

  private void updateCompareInfoCards(File imgFile, File vidFile) {
    if (tvCompareStillInfo != null) {
      tvCompareStillInfo.setText(buildStillImageDynamicInfo(imgFile));
    }

    if (tvCompareVideoInfo != null) {
      tvCompareVideoInfo.setText(buildVideoDynamicInfo(vidFile));
    }
  }

  private String buildStillImageDynamicInfo(File file) {
    StringBuilder sb = new StringBuilder();
    boolean hasGainmapXmp = checkImageHasGainmap(file);
    Bitmap bitmap = null;
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
      try {
        Context context = getContext();
        Uri photoUri = viewModel.getExtractPhotoUri().getValue();
        if (context != null && photoUri != null) {
          try {

            ImageDecoder.Source srcDirect =
                ImageDecoder.createSource(context.getContentResolver(), photoUri);

            bitmap =
                ImageDecoder.decodeBitmap(
                    srcDirect,
                    (decoder, info, s) -> decoder.setAllocator(ImageDecoder.ALLOCATOR_HARDWARE));
          } catch (Exception ignored) {

            // Ignored: Fall back to file-based ImageDecoder if direct content URI decoding fails.

          }
        }

        if (bitmap == null && file != null && file.exists()) {
          ImageDecoder.Source source = ImageDecoder.createSource(file);

          bitmap =
              ImageDecoder.decodeBitmap(
                  source,
                  (decoder, info, src) -> decoder.setAllocator(ImageDecoder.ALLOCATOR_HARDWARE));
        }

      } catch (Exception e) {
        Log.e("MP_IMG", "Error decoding for info", e);
      }
    }

    String colorSpaceName = "sRGB";
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
        && bitmap != null
        && bitmap.getColorSpace() != null) {
      colorSpaceName = bitmap.getColorSpace().getName();
    }

    sb.append("Color Space: ").append(colorSpaceName).append("\n");
    boolean hasGainmapObj = false;
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE && bitmap != null) {
      hasGainmapObj = bitmap.hasGainmap();
    }

    boolean gainmapPresent = hasGainmapXmp || hasGainmapObj;

    sb.append("Gain Map: ")
        .append(
            gainmapPresent ? (hasGainmapObj ? "ISO 21496-1 (Attached)" : "XMP Container") : "None")
        .append("\n");
    Bitmap.Config config = bitmap != null ? bitmap.getConfig() : null;
    sb.append("Format: ").append(config != null ? config.name() : "JPEG (Hardware)").append("\n");
    boolean isWindowHdr = false;
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && getActivity() != null) {
      isWindowHdr = (getActivity().getWindow().getColorMode() == ActivityInfo.COLOR_MODE_HDR);
    }

    sb.append("Color Mode: ")
        .append(isWindowHdr ? "COLOR_MODE_HDR" : "COLOR_MODE_DEFAULT")
        .append("\n");
    float liveRatio = 1.0f;
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE && getActivity() != null) {
      android.view.Display display = getActivity().getWindowManager().getDefaultDisplay();
      if (display != null) {
        liveRatio = display.getHdrSdrRatio();
      }
    }

    float headroomEv = (float) (Math.log(Math.max(1.0f, liveRatio)) / Math.log(2.0));

    sb.append("Display Headroom: ")
        .append(String.format(Locale.US, "%.2fx (+%.1f EV)", liveRatio, headroomEv));
    return sb.toString();
  }

  private String buildVideoDynamicInfo(File videoFile) {
    StringBuilder sb = new StringBuilder();
    if (videoFile == null || !videoFile.exists()) {
      return "No video extracted";
    }

    String mime = "video/mp4";
    String colorStandardStr = "BT.709 (sRGB)";
    String colorTransferStr = "SDR";
    int width = 0;
    int height = 0;
    int bitDepth = 8;
    MediaExtractor extractor = new MediaExtractor();
    try {
      extractor.setDataSource(videoFile.getAbsolutePath());
      for (int i = 0; i < extractor.getTrackCount(); i++) {
        MediaFormat format = extractor.getTrackFormat(i);

        String trackMime =
            format.containsKey(MediaFormat.KEY_MIME) ? format.getString(MediaFormat.KEY_MIME) : "";
        if (trackMime.startsWith("video/")) {
          mime = trackMime;
          if (format.containsKey(MediaFormat.KEY_WIDTH)) {
            width = format.getInteger(MediaFormat.KEY_WIDTH);
          }

          if (format.containsKey(MediaFormat.KEY_HEIGHT)) {
            height = format.getInteger(MediaFormat.KEY_HEIGHT);
          }

          if (format.containsKey(MediaFormat.KEY_COLOR_STANDARD)) {
            int std = format.getInteger(MediaFormat.KEY_COLOR_STANDARD);
            if (std == MediaFormat.COLOR_STANDARD_BT2020) {
              colorStandardStr = "BT.2020 (Wide Gamut)";
            } else if (std == MediaFormat.COLOR_STANDARD_BT709) {
              colorStandardStr = "BT.709 (sRGB)";
            } else if (std == MediaFormat.COLOR_STANDARD_BT601_NTSC
                || std == MediaFormat.COLOR_STANDARD_BT601_PAL) {
              colorStandardStr = "BT.601";
            } else {
              colorStandardStr = "Standard (" + std + ")";
            }
          }

          if (format.containsKey(MediaFormat.KEY_COLOR_TRANSFER)) {
            int transfer = format.getInteger(MediaFormat.KEY_COLOR_TRANSFER);
            if (transfer == MediaFormat.COLOR_TRANSFER_HLG) {
              colorTransferStr = "HLG (ARIB STD-B67)";
              bitDepth = 10;
            } else if (transfer == MediaFormat.COLOR_TRANSFER_ST2084) {
              colorTransferStr = "PQ (SMPTE ST 2084)";
              bitDepth = 10;
            } else if (transfer == MediaFormat.COLOR_TRANSFER_SDR_VIDEO) {
              colorTransferStr = "SDR (Gamma 2.2)";
            } else {
              colorTransferStr = "Transfer (" + transfer + ")";
            }
          }

          if (format.containsKey(MediaFormat.KEY_PROFILE)) {
            int profile = format.getInteger(MediaFormat.KEY_PROFILE);
            if (profile == 2 /* HEVCProfileMain10 */ || profile == 4096 /* VP9Profile2 */) {
              bitDepth = 10;
            }
          }

          break;
        }
      }

    } catch (Exception e) {
      Log.e("MP_VID", "Error inspecting video MediaFormat", e);
    } finally {
      extractor.release();
    }

    sb.append("Color Space: ").append(colorStandardStr).append("\n");
    sb.append("Transfer: ").append(colorTransferStr).append("\n");

    sb.append("Codec: ")
        .append(mime.replace("video/", "").toUpperCase(Locale.US))
        .append(" (")
        .append(bitDepth)
        .append("-bit ")
        .append(width)
        .append("x")
        .append(height)
        .append(")\n");
    boolean hasAgtm = (extractedAgtmJson != null && !extractedAgtmJson.trim().isEmpty());
    sb.append("AGTM: ").append(hasAgtm ? "ST 2094-50 Dynamic Splines" : "None").append("\n");
    sb.append("Surface: RGBA_1010102 (10-bit)\n");
    sb.append("Consistency: ");
    if (colorTransferStr.contains("HLG") || colorTransferStr.contains("PQ")) {
      sb.append("✨ Dynamic HDR Synchronized");
    } else {
      sb.append("SDR Base Synchronized");
    }

    return sb.toString();
  }

  private void updateExtractorPreviewSubTabContent(int position) {
    if (position != 1 && videoPlayer != null) {
      videoPlayer.pause();
    }

    if (position == 0) { // Image

      ivExtractorResultImage.setVisibility(View.VISIBLE);
      flExtractorResultVideoContainer.setVisibility(View.GONE);
      svExtractorResultMetadata.setVisibility(View.GONE);
      File extractedImg = viewModel.getExtractorExtractedImageFile().getValue();
      if (extractedImg != null && extractedImg.exists()) {
        loadImageFile(extractedImg);
      }

    } else if (position == 1) { // Video

      ivExtractorResultImage.setVisibility(View.GONE);
      flExtractorResultVideoContainer.setVisibility(View.VISIBLE);
      svExtractorResultMetadata.setVisibility(View.GONE);
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && getActivity() != null) {
        getActivity().getWindow().setColorMode(ActivityInfo.COLOR_MODE_HDR);
        try {

          java.lang.reflect.Method winMethod =
              android.view.Window.class.getMethod("setDesiredHdrHeadroom", float.class);
          winMethod.invoke(getActivity().getWindow(), 10.0f);
        } catch (Exception ignored) {

          // Ignored: setDesiredHdrHeadroom reflection is optional on unsupported platforms.

        }
      }

      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
        try {

          java.lang.reflect.Method viewMethod =
              View.class.getMethod("setDesiredHdrHeadroom", float.class);
          viewMethod.invoke(pvExtractorResultVideo, 10.0f);
          viewMethod.invoke(flExtractorResultVideoContainer, 10.0f);
        } catch (Exception ignored) {

          // Ignored: setDesiredHdrHeadroom reflection is optional on unsupported platforms.

        }
      }

      File extractedVid = viewModel.getExtractorExtractedVideoFile().getValue();
      if (extractedVid != null && extractedVid.exists()) {
        initializeVideoPlayer();
        videoPlayer.setMediaItem(MediaItem.fromUri(Uri.fromFile(extractedVid)));
        videoPlayer.prepare();
        videoPlayer.play();
      }

    } else if (position == 2) { // Metadata

      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && getActivity() != null) {
        getActivity().getWindow().setColorMode(ActivityInfo.COLOR_MODE_DEFAULT);
      }

      ivExtractorResultImage.setVisibility(View.GONE);
      flExtractorResultVideoContainer.setVisibility(View.GONE);
      svExtractorResultMetadata.setVisibility(View.VISIBLE);
      parseAndDisplayMetadata();
    }

    updateDownloadButtonState();
  }

  @Override
  public void onStart() {
    super.onStart();
    Context context = getContext();
    if (context != null) {
      Intent parserIntent = new Intent(context, MetadataParserService.class);
      context.bindService(parserIntent, parserConnection, Context.BIND_AUTO_CREATE);
      Intent extractorIntent = new Intent(context, MotionPhotoExtractorService.class);
      context.bindService(extractorIntent, extractorConnection, Context.BIND_AUTO_CREATE);
    }
  }

  @Override
  public void onStop() {
    super.onStop();
    Context context = getContext();
    if (context != null) {
      if (isParserBound) {
        context.unbindService(parserConnection);
        isParserBound = false;
      }

      if (isExtractorBound) {
        context.unbindService(extractorConnection);
        isExtractorBound = false;
      }
    }
  }

  private void ensureServiceBound() {
    Context context = getContext();
    if (context != null) {
      if (!isParserBound) {
        Intent intent = new Intent(context, MetadataParserService.class);
        context.bindService(intent, parserConnection, Context.BIND_AUTO_CREATE);
      }

      if (!isExtractorBound) {
        Intent intent = new Intent(context, MotionPhotoExtractorService.class);
        context.bindService(intent, extractorConnection, Context.BIND_AUTO_CREATE);
      }
    }
  }

  private void parseAndDisplayMetadata() {
    if (!isParserBound || metadataParserService == null) {
      ensureServiceBound();
      clMetadataBlocksContainer.removeAllViews();
      TextView tvBinding = new TextView(clMetadataBlocksContainer.getContext());
      tvBinding.setText("Binding to metadata parser service...");
      tvBinding.setTextColor(0xFF666666);
      addBlockToContainer(tvBinding);
      return;
    }

    Uri uri = viewModel.getExtractPhotoUri().getValue();
    if (uri == null) {
      showMetadataError("No input image URI.");
      return;
    }

    Context context = getContext();
    if (context == null) {
      showMetadataError("Context is null.");
      return;
    }

    clMetadataBlocksContainer.removeAllViews();
    TextView tvLoading = new TextView(clMetadataBlocksContainer.getContext());
    tvLoading.setText("Parsing metadata...");
    addBlockToContainer(tvLoading);

    executor.execute(
        () -> {
          try {
            byte[] collectionBytes;
            try (ParcelFileDescriptor pfd = StorageUtils.openParcelFileDescriptor(context, uri)) {
              if (pfd == null) {
                showMetadataErrorOnUi("Failed to open file descriptor.");
                return;
              }

              collectionBytes = metadataParserService.parseMetadata(pfd);
            }

            if (collectionBytes == null) {
              showMetadataErrorOnUi("Failed to parse metadata (null result).");
              return;
            }

            String agtmJson = null;
            Uri photoUri = viewModel.getExtractPhotoUri().getValue();
            if (photoUri != null) {
              try (ParcelFileDescriptor agtmPfd =
                  StorageUtils.openParcelFileDescriptor(context, photoUri)) {
                if (agtmPfd != null) {
                  agtmJson = metadataParserService.extractAgtm(agtmPfd);
                }

              } catch (Exception e) {
                viewModel.appendExtractorLog("Error extracting AGTM: " + e.getMessage());
              }
            }

            String finalAgtmJson = agtmJson;
            Activity activity = getActivity();
            if (activity != null) {
              activity.runOnUiThread(() -> displayMetadataNatively(collectionBytes, finalAgtmJson));
            }

          } catch (Exception e) {
            showMetadataErrorOnUi("Error: " + e.getMessage());
          }
        });
  }

  private void addBlockToContainer(View view) {
    if (clMetadataBlocksContainer == null) {
      return;
    }
    int childCount = clMetadataBlocksContainer.getChildCount();
    view.setId(View.generateViewId());
    ConstraintLayout.LayoutParams lp =
        new ConstraintLayout.LayoutParams(
            ConstraintLayout.LayoutParams.MATCH_PARENT, ConstraintLayout.LayoutParams.WRAP_CONTENT);
    lp.startToStart = ConstraintLayout.LayoutParams.PARENT_ID;
    lp.endToEnd = ConstraintLayout.LayoutParams.PARENT_ID;
    if (childCount == 0) {
      lp.topToTop = ConstraintLayout.LayoutParams.PARENT_ID;
    } else {
      View prevView = clMetadataBlocksContainer.getChildAt(childCount - 1);
      lp.topToBottom = prevView.getId();
    }
    lp.topMargin = (int) (8 * view.getResources().getDisplayMetrics().density);
    view.setLayoutParams(lp);
    clMetadataBlocksContainer.addView(view);
  }
  private void displayMetadataNatively(byte[] collectionBytes, String agtmJson) {
    this.extractedAgtmJson = agtmJson;
    clMetadataBlocksContainer.removeAllViews();
    LayoutInflater inflater = LayoutInflater.from(clMetadataBlocksContainer.getContext());
    try {
      MetadataCollection collection = MetadataCollection.parseFrom(collectionBytes);
      if (collection.getBlocksCount() == 0 && (agtmJson == null || agtmJson.trim().isEmpty())) {
        TextView tvEmpty = new TextView(clMetadataBlocksContainer.getContext());
        tvEmpty.setText("No metadata blocks found.");
        addBlockToContainer(tvEmpty);
        return;
      }

      for (int i = 0; i < collection.getBlocksCount(); i++) {
        MetadataBlock block = collection.getBlocks(i);

        View itemView =
            inflater.inflate(R.layout.item_metadata_block, clMetadataBlocksContainer, false);
        TextView tvTitle = itemView.findViewById(R.id.tv_block_title);
        TextView tvSummary = itemView.findViewById(R.id.tv_block_summary);
        TextView tvDetails = itemView.findViewById(R.id.tv_block_details);
        String type = block.getType().name();
        String formatId = block.getFormatIdentifier();
        int rawSize = block.getRawBytes().size();
        tvTitle.setText("Block " + i + ": " + type);
        tvSummary.setText("Format: " + formatId + " (" + rawSize + " bytes)");
        String detailsText;
        String typeUrl = block.getPayloadTypeUrl();
        if (typeUrl.endsWith("MotionPhotoMetadata")) {
          try {

            MotionPhotoMetadata motionPhotoMetadata =
                MotionPhotoMetadata.parseFrom(
                    block.getDecodedPayloadBytes(), ExtensionRegistryLite.getEmptyRegistry());
            detailsText = motionPhotoMetadata.toString();
          } catch (InvalidProtocolBufferException e) {
            detailsText = "Failed to parse MotionPhotoMetadata: " + e.getMessage();
          }

        } else if (rawSize > 2048) {

          detailsText =
              "Payload is too large to display (" + rawSize + " bytes).\nType URL: " + typeUrl;
        } else {
          detailsText = block.toString();
        }

        tvDetails.setText(detailsText);

        itemView.setOnClickListener(
            v -> {
              if (tvDetails.getVisibility() == View.GONE) {
                tvDetails.setVisibility(View.VISIBLE);
              } else {
                tvDetails.setVisibility(View.GONE);
              }
            });
        addBlockToContainer(itemView);
      }

      if (agtmJson != null && !agtmJson.isEmpty() && !agtmJson.trim().isEmpty()) {

        View agtmView =
            inflater.inflate(R.layout.item_metadata_block, clMetadataBlocksContainer, false);
        TextView tvTitle = agtmView.findViewById(R.id.tv_block_title);
        TextView tvSummary = agtmView.findViewById(R.id.tv_block_summary);
        TextView tvDetails = agtmView.findViewById(R.id.tv_block_details);
        tvTitle.setText("AGTM: SMPTE ST 2094-50");
        tvSummary.setText("Extracted at presentation timestamp 0 us");
        try {
          JSONObject agtmObj = new JSONObject(agtmJson);
          tvDetails.setText(agtmObj.toString(2));
        } catch (org.json.JSONException e) {
          tvDetails.setText(agtmJson);
        }

        agtmView.setOnClickListener(
            v -> {
              if (tvDetails.getVisibility() == View.GONE) {
                tvDetails.setVisibility(View.VISIBLE);
              } else {
                tvDetails.setVisibility(View.GONE);
              }
            });
        addBlockToContainer(agtmView);
      }

    } catch (com.google.protobuf.InvalidProtocolBufferException e) {
      showMetadataError("Proto Parse Error: " + e.getMessage());
    }
  }

  private void showMetadataError(String message) {
    clMetadataBlocksContainer.removeAllViews();
    TextView tvError = new TextView(clMetadataBlocksContainer.getContext());
    tvError.setText(message);

    tvError.setTextColor(0xFFFF0000); // Red color

    addBlockToContainer(tvError);
  }

  private void showMetadataErrorOnUi(String message) {
    Activity activity = getActivity();
    if (activity != null) {
      activity.runOnUiThread(() -> showMetadataError(message));
    }
  }

  private void extractMotionPhoto() {
    if (!isExtracting.compareAndSet(false, true)) {
      return;
    }

    viewModel.clearExtractorLogs();
    viewModel.appendExtractorLog("Starting extraction process...");
    viewModel.setExtractorStatus("Extracting...");
    final Context context = getContext();
    if (context == null) {
      isExtracting.set(false);
      return;
    }

    executor.execute(
        () -> {
          try {
            int waitMs = 0;

            while ((!isParserBound
                    || metadataParserService == null
                    || !isExtractorBound
                    || extractorService == null)
                && waitMs < 5000) {
              try {
                Thread.sleep(50);
              } catch (InterruptedException ignored) {
              }

              waitMs += 50;
            }

            viewModel.appendExtractorLog("Copying input photo to temp file...");

            String inputPath =
                StorageUtils.copyUriToTempFile(
                    context, viewModel.getExtractPhotoUri().getValue(), "extractor_input", ".jpg");
            if (inputPath == null) {
              viewModel.appendExtractorLog("Failed to copy input file.");
              viewModel.setExtractorStatus("Failed to copy input file.");
              return;
            }

            viewModel.appendExtractorLog("Checking if input file is a motion photo via service...");
            boolean isMotionPhoto = false;
            Uri uri = viewModel.getExtractPhotoUri().getValue();
            if (isParserBound && metadataParserService != null) {
              try (ParcelFileDescriptor pfd = StorageUtils.openParcelFileDescriptor(context, uri)) {
                if (pfd != null) {
                  byte[] collectionBytes = metadataParserService.parseMetadata(pfd);
                  if (collectionBytes != null) {

                    boolean disable3p =
                        Boolean.TRUE.equals(viewModel.getDisable3pPlugins().getValue());

                    isMotionPhoto =
                        metadataParserService.isMotionPhoto(
                            collectionBytes, disable3p, new java.util.ArrayList<>());
                    viewModel.appendExtractorLog(
                        "Service isMotionPhoto returned: " + isMotionPhoto);
                  } else {
                    viewModel.appendExtractorLog(
                        "Service parseMetadata returned null during check");
                  }

                } else {
                  viewModel.appendExtractorLog("Failed to open PFD for check");
                }

              } catch (IOException e) {
                viewModel.appendExtractorLog("Error opening PFD for check: " + e.getMessage());
              }

            } else {
              viewModel.appendExtractorLog(
                  "Parser service not bound or URI null, cannot perform check");
            }

            if (!isMotionPhoto) {
              viewModel.appendExtractorLog("Input file is NOT a valid motion photo.");
              viewModel.setExtractorStatus("Not a motion photo.");
              new File(inputPath).delete();
              return;
            }

            String ext = inputPath.substring(inputPath.lastIndexOf('.'));

            File extractedImageFile =
                File.createTempFile("extractor_extracted_photo", ext, context.getCacheDir());

            File extractedVideoFile =
                File.createTempFile("extractor_extracted_video", ".mp4", context.getCacheDir());

            File extractedMetadataFile =
                File.createTempFile("extractor_extracted_metadata", ".xml", context.getCacheDir());
            viewModel.appendExtractorLog("Extracting files via MotionPhotoExtractorService...");
            int extractResult = -1;
            if (isExtractorBound && extractorService != null) {

              extractResult =
                  extractorService.extractMotionPhoto(
                      inputPath,
                      extractedImageFile.getAbsolutePath(),
                      extractedVideoFile.getAbsolutePath(),
                      extractedMetadataFile.getAbsolutePath());
            } else {
              viewModel.appendExtractorLog("Extractor service not bound, cannot extract files.");
            }

            viewModel.appendExtractorLog("Extractor returned: " + extractResult);
            if (extractResult == 0) {
              viewModel.appendExtractorLog("Extraction successful.");
              viewModel.setExtractorExtractedImageFile(extractedImageFile);
              viewModel.setExtractorExtractedVideoFile(extractedVideoFile);
              viewModel.setExtractorExtractedMetadataFile(extractedMetadataFile);
            } else {
              viewModel.appendExtractorLog("Extraction failed.");
              viewModel.setExtractorStatus("IS motion photo (extraction failed).");
              extractedImageFile.delete();
              extractedVideoFile.delete();
              extractedMetadataFile.delete();
            }

            // Clean up temp input

            new File(inputPath).delete();
          } catch (IOException e) {
            viewModel.appendExtractorLog("IOException during extraction: " + e.getMessage());
            viewModel.setExtractorStatus("Error: " + e.getMessage());
          } catch (Exception e) {
            viewModel.appendExtractorLog("Error during extraction: " + e.getMessage());
            viewModel.setExtractorStatus("Error: " + e.getMessage());
          } finally {
            isExtracting.set(false);
          }
        });
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
      viewModel.appendExtractorLog("No pending file to save or file doesn't exist.");
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
                viewModel.appendExtractorLog("File saved successfully.");
              } else {
                viewModel.appendExtractorLog("Failed to open output stream for destination.");
              }
            }

          } catch (IOException e) {
            viewModel.appendExtractorLog("Failed to save file: " + e.getMessage());
          }
        });
  }

  private void updateDownloadButtonState() {

    View btnDownload =
        getView() != null ? getView().findViewById(R.id.btn_extractor_download) : null;
    if (btnDownload == null) {
      return;
    }

    int mainTabPos = extractorTabLayout.getSelectedTabPosition();
    if (mainTabPos == 1) { // Logs

      btnDownload.setVisibility(View.GONE);
      return;
    }

    // Preview tab

    int subTabPos = extractorPreviewSubTabLayout.getSelectedTabPosition();
    if (subTabPos == 0) { // Image

      File imgFile = viewModel.getExtractorExtractedImageFile().getValue();
      if (imgFile != null && imgFile.exists()) {
        btnDownload.setVisibility(View.VISIBLE);
        ((Button) btnDownload).setText("Download Image");
      } else {
        btnDownload.setVisibility(View.GONE);
      }

    } else if (subTabPos == 1) { // Video

      File vidFile = viewModel.getExtractorExtractedVideoFile().getValue();
      if (vidFile != null && vidFile.exists()) {
        btnDownload.setVisibility(View.VISIBLE);
        ((Button) btnDownload).setText("Download Video");
      } else {
        btnDownload.setVisibility(View.GONE);
      }

    } else { // Metadata

      btnDownload.setVisibility(View.GONE);
    }
  }

  private boolean loadImageFile(File file) {
    return loadImageFileIntoView(file, ivExtractorResultImage);
  }

  private boolean loadImageFileIntoView(File file, ImageView targetView) {
    boolean hasGainmap = checkImageHasGainmap(file);
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
      try {
        Context context = getContext();
        Uri photoUri = viewModel.getExtractPhotoUri().getValue();
        Bitmap bitmap = null;
        if (context != null && photoUri != null) {
          try {

            ImageDecoder.Source srcDirect =
                ImageDecoder.createSource(context.getContentResolver(), photoUri);

            bitmap =
                ImageDecoder.decodeBitmap(
                    srcDirect,
                    (decoder, info, s) -> {
                      decoder.setAllocator(ImageDecoder.ALLOCATOR_HARDWARE);
                    });
          } catch (Exception ignored) {

            // Ignored: Fall back to file-based ImageDecoder if direct content URI decoding fails.

          }
        }

        if (bitmap == null) {
          ImageDecoder.Source source = ImageDecoder.createSource(file);

          bitmap =
              ImageDecoder.decodeBitmap(
                  source,
                  (decoder, info, src) -> {
                    decoder.setAllocator(ImageDecoder.ALLOCATOR_HARDWARE);
                  });
        }

        targetView.setImageBitmap(bitmap);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
          boolean bHasGainmap = bitmap.hasGainmap();
          hasGainmap = hasGainmap || bHasGainmap;
          android.graphics.Gainmap gm = bitmap.getGainmap();

          Log.d(
              "MP_IMG",
              "Bitmap hasGainmap="
                  + bHasGainmap
                  + ", gainmapObj="
                  + (gm != null ? gm.toString() : "null"));
          try {

            java.lang.reflect.Method viewMethod =
                View.class.getMethod("setDesiredHdrHeadroom", float.class);
            viewMethod.invoke(targetView, hasGainmap ? 10.0f : 1.0f);
          } catch (Exception ignored) {

            // Ignored: setDesiredHdrHeadroom reflection is optional on unsupported platforms.

          }
        }

        if (getActivity() != null) {

          getActivity()
              .getWindow()
              .setColorMode(
                  hasGainmap ? ActivityInfo.COLOR_MODE_HDR : ActivityInfo.COLOR_MODE_DEFAULT);
          try {

            java.lang.reflect.Method winMethod =
                android.view.Window.class.getMethod("setDesiredHdrHeadroom", float.class);
            winMethod.invoke(getActivity().getWindow(), hasGainmap ? 10.0f : 1.0f);
          } catch (Exception ignored) {

            // Ignored: setDesiredHdrHeadroom reflection is optional on unsupported platforms.

          }
        }

        Log.d("MP_IMG", "loadImageFile final hasGainmap=" + hasGainmap);
        return hasGainmap;
      } catch (Exception e) {
        Log.e("MP_IMG", "Error decoding bitmap with ImageDecoder", e);
        targetView.setImageURI(Uri.fromFile(file));
        return hasGainmap;
      }

    } else {
      targetView.setImageURI(Uri.fromFile(file));
      return hasGainmap;
    }
  }

  private boolean checkImageHasGainmap(File file) {
    if (file == null || !file.exists()) {
      return false;
    }

    try (java.io.FileInputStream fis = new java.io.FileInputStream(file)) {
      byte[] header = new byte[(int) Math.min(file.length(), 65536)];
      int read = fis.read(header);
      if (read > 0) {
        String str = new String(header, 0, read, java.nio.charset.StandardCharsets.ISO_8859_1);
        if (str.contains("hdrgm")
            || str.contains("GainMap")
            || str.contains("21496")
            || str.contains("ultrahdr")) {
          return true;
        }
      }

    } catch (Exception ignored) {

      // Ignored: Non-critical heuristic check for UltraHDR/Gainmap markers.

    }

    return false;
  }

  private void updateImageTabHdrState(boolean isHdr) {
    TabLayout.Tab imageTab = extractorPreviewSubTabLayout.getTabAt(0);
    if (imageTab != null) {
      imageTab.setText("Image");
    }
  }

  private void updateVideoTabHdrState(boolean isHdr) {
    TabLayout.Tab videoTab = extractorPreviewSubTabLayout.getTabAt(1);
    if (videoTab != null) {
      videoTab.setText("Video");
    }
  }
}
