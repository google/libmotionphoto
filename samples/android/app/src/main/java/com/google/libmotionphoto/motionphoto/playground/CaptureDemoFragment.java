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

import android.Manifest;
import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.ImageFormat;
import android.graphics.Matrix;
import android.graphics.RectF;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.Image;
import android.media.ImageReader;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.media.MediaRecorder;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.util.Log;
import android.util.Size;
import android.util.SparseIntArray;
import android.view.LayoutInflater;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ScrollView;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import com.google.libmotionphoto.MotionPhotoBuilderService;
import com.google.libmotionphoto.MotionPhotoExtractorService;
import com.google.libmotionphoto.IMotionPhotoBuilderService;
import com.google.libmotionphoto.IMotionPhotoExtractorService;
import java.io.File;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedList;
import java.util.List;

/**
 * Fragment demonstrating live simultaneous Camera photo + video capture using a continuous encoded
 * ring buffer and libmotionphoto assembly.
 */
@android.annotation.SuppressLint({"NewApi", "SetTextI18n"})
public class CaptureDemoFragment extends Fragment {
  private static final String TAG = "MPCaptureDemo";
  private static final int REQUEST_CAMERA_PERMISSION = 200;

  // Ring Buffer Config: 1.5s past + 1.5s future (total ~3.0s Motion Photo video window)
  private static final long PAST_WINDOW_US = 1_500_000L; // 1.5 seconds
  private static final long FUTURE_WINDOW_US = 1_500_000L; // 1.5 seconds

  private AutoFitTextureView textureView;
  private TextView tvBufferStatus;
  private TextView tvCaptureLogs;
  private ScrollView svLogs;
  private Button btnShutter;
  private Button btnMainMenu;

  private PlaygroundViewModel viewModel;
  private CameraDevice cameraDevice;
  private CameraCaptureSession captureSession;
  private Surface encoderSurface;
  private MediaCodec videoEncoder;
  private ImageReader stillImageReader;

  private IMotionPhotoBuilderService builderService;
  private boolean isBuilderBound = false;
  private IMotionPhotoExtractorService extractorService;
  private boolean isExtractorBound = false;

  private final ServiceConnection builderConnection =
      new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
          builderService = IMotionPhotoBuilderService.Stub.asInterface(service);
          isBuilderBound = true;
          appendLog("Bound to MotionPhotoBuilderService for capture.");
        }

        @Override
        public void onServiceDisconnected(ComponentName arg0) {
          builderService = null;
          isBuilderBound = false;
          appendLog("Disconnected from MotionPhotoBuilderService.");
        }
      };

  private final ServiceConnection extractorConnection =
      new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
          extractorService = IMotionPhotoExtractorService.Stub.asInterface(service);
          isExtractorBound = true;
          appendLog("Bound to MotionPhotoExtractorService for preview extraction.");
        }

        @Override
        public void onServiceDisconnected(ComponentName arg0) {
          extractorService = null;
          isExtractorBound = false;
          appendLog("Disconnected from MotionPhotoExtractorService.");
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

  private HandlerThread backgroundThread;
  private Handler backgroundHandler;

  // Video Encoding & Ring Buffer
  private final LinkedList<EncodedFrame> ringBuffer = new LinkedList<>();
  private ByteBuffer csd0;
  private ByteBuffer csd1;
  private boolean isEncoding = false;
  private boolean isCapturing = false;

  // Audio Encoding & Ring Buffer
  private AudioRecord audioRecord;
  private MediaCodec audioEncoder;
  private final LinkedList<EncodedFrame> audioRingBuffer = new LinkedList<>();
  private ByteBuffer audioCsd0;
  private boolean isAudioEncoding = false;

  // Reusable Memory Buffer Pools (Zero GC allocation during continuous capture)
  private final ByteArrayPool videoBufferPool = new ByteArrayPool(160);
  private final ByteArrayPool audioBufferPool = new ByteArrayPool(260);

  private static class ByteArrayPool {
    private final LinkedList<byte[]> pool = new LinkedList<>();
    private final int maxPoolSize;

    ByteArrayPool(int maxPoolSize) {
      this.maxPoolSize = maxPoolSize;
    }

    synchronized byte[] acquire(int minSize) {
      for (int i = 0; i < pool.size(); i++) {
        byte[] b = pool.get(i);
        if (b.length >= minSize) {
          pool.remove(i);
          return b;
        }
      }
      return new byte[Math.max(minSize, 1024)];
    }

    synchronized void release(byte[] b) {
      if (b != null && pool.size() < maxPoolSize) {
        pool.addLast(b);
      }
    }
  }

  private static class EncodedFrame {
    final byte[] data;
    final int size;
    final int flags;
    final long ptsUs;

    EncodedFrame(byte[] data, int size, int flags, long ptsUs) {
      this.data = data;
      this.size = size;
      this.flags = flags;
      this.ptsUs = ptsUs;
    }
  }

  @Override
  public View onCreateView(
      LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    View view = inflater.inflate(R.layout.fragment_capture_demo, container, false);
    viewModel = new ViewModelProvider(requireActivity()).get(PlaygroundViewModel.class);

    textureView = view.findViewById(R.id.texture_view);
    tvBufferStatus = view.findViewById(R.id.tv_buffer_status);
    tvCaptureLogs = view.findViewById(R.id.tv_capture_logs);
    svLogs = view.findViewById(R.id.sv_logs);
    btnShutter = view.findViewById(R.id.btn_shutter);
    btnMainMenu = view.findViewById(R.id.btn_main_menu);

    btnShutter.setOnClickListener(v -> triggerMotionPhotoCapture());
    btnMainMenu.setOnClickListener(v -> ((MainActivity) requireActivity()).navigateToTriage());

    textureView.addOnLayoutChangeListener(
        (v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
          int width = right - left;
          int height = bottom - top;
          if (width > 0 && height > 0) {
            configureTransform(width, height);
          }
        });

    textureView.setSurfaceTextureListener(textureListener);
    return view;
  }

  @Override
  public void onConfigurationChanged(@NonNull Configuration newConfig) {
    super.onConfigurationChanged(newConfig);
    if (textureView != null && textureView.isAvailable()) {
      textureView.post(() -> configureTransform(textureView.getWidth(), textureView.getHeight()));
    }
  }

  private static final SparseIntArray ORIENTATIONS = new SparseIntArray();

  static {
    ORIENTATIONS.append(Surface.ROTATION_0, 90);
    ORIENTATIONS.append(Surface.ROTATION_90, 0);
    ORIENTATIONS.append(Surface.ROTATION_180, 270);
    ORIENTATIONS.append(Surface.ROTATION_270, 180);
  }

  private int sensorOrientation = 90;

  private final TextureView.SurfaceTextureListener textureListener =
      new TextureView.SurfaceTextureListener() {
        @Override
        public void onSurfaceTextureAvailable(
            @NonNull SurfaceTexture surface, int width, int height) {
          startBackgroundThread();
          checkAndOpenCamera();
          configureTransform(width, height);
        }

        @Override
        public void onSurfaceTextureSizeChanged(
            @NonNull SurfaceTexture surface, int width, int height) {
          configureTransform(width, height);
        }

        @Override
        public boolean onSurfaceTextureDestroyed(@NonNull SurfaceTexture surface) {
          closeCamera();
          stopBackgroundThread();
          return true;
        }

        @Override
        public void onSurfaceTextureUpdated(@NonNull SurfaceTexture surface) {}
      };

  private void configureTransform(int viewWidth, int viewHeight) {
    Activity activity = getActivity();
    if (textureView == null || activity == null || viewWidth == 0 || viewHeight == 0) {
      return;
    }
    int rotation = activity.getWindowManager().getDefaultDisplay().getRotation();

    if (getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE) {
      textureView.setAspectRatio(videoWidth, videoHeight);
    } else {
      textureView.setAspectRatio(videoHeight, videoWidth);
    }

    Matrix matrix = new Matrix();
    RectF viewRect = new RectF(0, 0, viewWidth, viewHeight);
    RectF bufferRect = new RectF(0, 0, videoHeight, videoWidth);
    float centerX = viewRect.centerX();
    float centerY = viewRect.centerY();
    if (Surface.ROTATION_90 == rotation || Surface.ROTATION_270 == rotation) {
      bufferRect.offset(centerX - bufferRect.centerX(), centerY - bufferRect.centerY());
      matrix.setRectToRect(viewRect, bufferRect, Matrix.ScaleToFit.FILL);
      float scale = Math.max((float) viewHeight / videoHeight, (float) viewWidth / videoWidth);
      matrix.postScale(scale, scale, centerX, centerY);
      matrix.postRotate(90 * (rotation - 2), centerX, centerY);
    } else if (Surface.ROTATION_180 == rotation) {
      matrix.postRotate(180, centerX, centerY);
    }

    textureView.setTransform(matrix);
  }

  private void checkAndOpenCamera() {
    if (ActivityCompat.checkSelfPermission(requireContext(), Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED
        || ActivityCompat.checkSelfPermission(requireContext(), Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED) {
      requestPermissions(
          new String[] {Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO},
          REQUEST_CAMERA_PERMISSION);
      return;
    }
    openCamera();
  }

  @Override
  public void onRequestPermissionsResult(
      int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
    if (requestCode == REQUEST_CAMERA_PERMISSION) {
      if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
        openCamera();
      } else {
        appendLog("Camera/Audio permission denied.");
        updateStatus("Permission Denied");
      }
    }
  }

  private int videoWidth = 1920;
  private int videoHeight = 1080;

  private void openCamera() {
    CameraManager manager =
        (CameraManager) requireActivity().getSystemService(Context.CAMERA_SERVICE);
    try {
      String cameraId = manager.getCameraIdList()[0];
      CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);
      Integer sensorOrient = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION);
      if (sensorOrient != null) {
        sensorOrientation = sensorOrient;
      }
      StreamConfigurationMap map =
          characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

      Size stillSize = new Size(1920, 1080);
      Size videoSize = new Size(1920, 1080);

      if (map != null) {
        Size[] jpegSizes = map.getOutputSizes(ImageFormat.JPEG);
        Size[] mediaRecorderSizes = map.getOutputSizes(android.media.MediaRecorder.class);

        // Find a matching 16:9 1080p size or closest matching resolution for both
        if (jpegSizes != null && jpegSizes.length > 0) {
          boolean found1080p = false;
          for (Size s : jpegSizes) {
            if (s.getWidth() == 1920 && s.getHeight() == 1080) {
              stillSize = s;
              found1080p = true;
              break;
            }
          }
          if (!found1080p) {
            // Find first 16:9 size
            for (Size s : jpegSizes) {
              if (Math.abs((double) s.getWidth() / s.getHeight() - (16.0 / 9.0)) < 0.05) {
                stillSize = s;
                break;
              }
            }
          }
        }

        if (mediaRecorderSizes != null && mediaRecorderSizes.length > 0) {
          boolean foundMatch = false;
          for (Size s : mediaRecorderSizes) {
            if (s.getWidth() == stillSize.getWidth() && s.getHeight() == stillSize.getHeight()) {
              videoSize = s;
              foundMatch = true;
              break;
            }
          }
          if (!foundMatch) {
            // Fall back to 1080p or closest 16:9 size
            for (Size s : mediaRecorderSizes) {
              if (s.getWidth() == 1920 && s.getHeight() == 1080) {
                videoSize = s;
                break;
              }
            }
          }
        }
      }

      videoWidth = videoSize.getWidth();
      videoHeight = videoSize.getHeight();

      if (textureView.isAvailable()) {
        configureTransform(textureView.getWidth(), textureView.getHeight());
      }

      appendLog(
          "Matched Photo: "
              + stillSize.getWidth()
              + "x"
              + stillSize.getHeight()
              + " | Video: "
              + videoWidth
              + "x"
              + videoHeight);

      setupMediaEncoder(videoWidth, videoHeight);
      setupAudioEncoder();
      stillImageReader =
          ImageReader.newInstance(stillSize.getWidth(), stillSize.getHeight(), ImageFormat.JPEG, 2);

      if (ActivityCompat.checkSelfPermission(requireContext(), Manifest.permission.CAMERA)
          == PackageManager.PERMISSION_GRANTED) {
        manager.openCamera(cameraId, stateCallback, backgroundHandler);
        appendLog("Opening camera: " + cameraId);
      }
    } catch (CameraAccessException | IOException e) {
      appendLog("Error opening camera: " + e.getMessage());
    }
  }

  private final CameraDevice.StateCallback stateCallback =
      new CameraDevice.StateCallback() {
        @Override
        public void onOpened(@NonNull CameraDevice camera) {
          cameraDevice = camera;
          createCaptureSession();
        }

        @Override
        public void onDisconnected(@NonNull CameraDevice camera) {
          camera.close();
          cameraDevice = null;
        }

        @Override
        public void onError(@NonNull CameraDevice camera, int error) {
          camera.close();
          cameraDevice = null;
          appendLog("Camera error code: " + error);
        }
      };

  private void setupMediaEncoder(int width, int height) throws IOException {
    MediaFormat format =
        MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_HEVC, width, height);
    format.setInteger(
        MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
    format.setInteger(MediaFormat.KEY_BIT_RATE, 8_000_000); // 8 Mbps
    format.setInteger(MediaFormat.KEY_FRAME_RATE, 30);
    format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1); // 1 keyframe per second

    videoEncoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_HEVC);
    videoEncoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
    encoderSurface = videoEncoder.createInputSurface();
    videoEncoder.start();
    isEncoding = true;

    startEncoderLoop();
    appendLog("MediaCodec HEVC Video Encoder started (" + width + "x" + height + ").");
  }

  private void startEncoderLoop() {
    backgroundHandler.post(
        new Runnable() {
          @Override
          public void run() {
            if (!isEncoding || videoEncoder == null) {
              return;
            }
            try {
              MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
              int index = videoEncoder.dequeueOutputBuffer(bufferInfo, 10_000);
              while (index >= 0) {
                ByteBuffer encodedData = videoEncoder.getOutputBuffer(index);
                if (encodedData != null
                    && (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                  // Save CSD (SPS/PPS)
                  byte[] configData = new byte[bufferInfo.size];
                  encodedData.position(bufferInfo.offset);
                  encodedData.get(configData, 0, bufferInfo.size);
                  csd0 = ByteBuffer.wrap(configData);
                  videoEncoder.releaseOutputBuffer(index, false);
                  index = videoEncoder.dequeueOutputBuffer(bufferInfo, 0);
                  continue;
                }

                if (bufferInfo.size > 0
                    && (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                  encodedData.position(bufferInfo.offset);
                  encodedData.limit(bufferInfo.offset + bufferInfo.size);
                  byte[] frameData = videoBufferPool.acquire(bufferInfo.size);
                  encodedData.get(frameData, 0, bufferInfo.size);

                  synchronized (ringBuffer) {
                    ringBuffer.addLast(
                        new EncodedFrame(
                            frameData,
                            bufferInfo.size,
                            bufferInfo.flags,
                            bufferInfo.presentationTimeUs));
                    // Keep max ~150 frames (~5s) in memory
                    while (ringBuffer.size() > 150) {
                      EncodedFrame removed = ringBuffer.removeFirst();
                      videoBufferPool.release(removed.data);
                    }
                  }
                }
                videoEncoder.releaseOutputBuffer(index, false);
                index = videoEncoder.dequeueOutputBuffer(bufferInfo, 0);
              }
            } catch (Exception e) {
              Log.e(TAG, "Encoder loop error: " + e.getMessage());
            }

            if (isEncoding) {
              backgroundHandler.postDelayed(this, 10);
            }
          }
        });
  }

  private void setupAudioEncoder() {
    try {
      int sampleRate = 44100;
      int channelConfig = AudioFormat.CHANNEL_IN_MONO;
      int audioFormatEnum = AudioFormat.ENCODING_PCM_16BIT;
      int minBufferSize = AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormatEnum);

      if (ActivityCompat.checkSelfPermission(requireContext(), Manifest.permission.RECORD_AUDIO)
          != PackageManager.PERMISSION_GRANTED) {
        appendLog("RECORD_AUDIO permission not granted for AAC encoding.");
        return;
      }

      audioRecord =
          new AudioRecord(
              MediaRecorder.AudioSource.MIC,
              sampleRate,
              channelConfig,
              audioFormatEnum,
              Math.max(minBufferSize * 2, 4096));

      MediaFormat format =
          MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, 1);
      format.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC);
      format.setInteger(MediaFormat.KEY_BIT_RATE, 64_000);
      format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384);

      audioEncoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC);
      audioEncoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
      audioEncoder.start();
      audioRecord.startRecording();
      isAudioEncoding = true;

      startAudioLoop();
      appendLog("MediaCodec AAC Audio Encoder & AudioRecord started (44.1kHz Mono).");
    } catch (Exception e) {
      appendLog("Audio Encoder initialization error: " + e.getMessage());
    }
  }

  private void startAudioLoop() {
    backgroundHandler.post(
        new Runnable() {
          @Override
          public void run() {
            if (!isAudioEncoding || audioEncoder == null || audioRecord == null) {
              return;
            }
            try {
              byte[] pcmBuffer = new byte[2048];
              int readBytes = audioRecord.read(pcmBuffer, 0, pcmBuffer.length);
              if (readBytes > 0) {
                int inputBufferIndex = audioEncoder.dequeueInputBuffer(1000);
                if (inputBufferIndex >= 0) {
                  ByteBuffer inputBuffer = audioEncoder.getInputBuffer(inputBufferIndex);
                  if (inputBuffer != null) {
                    inputBuffer.clear();
                    inputBuffer.put(pcmBuffer, 0, readBytes);
                    long ptsUs = SystemClock.elapsedRealtimeNanos() / 1000L;
                    audioEncoder.queueInputBuffer(inputBufferIndex, 0, readBytes, ptsUs, 0);
                  }
                }
              }

              MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
              int outputIndex = audioEncoder.dequeueOutputBuffer(bufferInfo, 0);
              while (outputIndex >= 0) {
                ByteBuffer encodedData = audioEncoder.getOutputBuffer(outputIndex);
                if (encodedData != null
                    && (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                  byte[] configData = new byte[bufferInfo.size];
                  encodedData.position(bufferInfo.offset);
                  encodedData.get(configData, 0, bufferInfo.size);
                  audioCsd0 = ByteBuffer.wrap(configData);
                  audioEncoder.releaseOutputBuffer(outputIndex, false);
                  outputIndex = audioEncoder.dequeueOutputBuffer(bufferInfo, 0);
                  continue;
                }

                if (bufferInfo.size > 0
                    && (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                  encodedData.position(bufferInfo.offset);
                  encodedData.limit(bufferInfo.offset + bufferInfo.size);
                  byte[] frameData = audioBufferPool.acquire(bufferInfo.size);
                  encodedData.get(frameData, 0, bufferInfo.size);

                  synchronized (audioRingBuffer) {
                    audioRingBuffer.addLast(
                        new EncodedFrame(
                            frameData,
                            bufferInfo.size,
                            bufferInfo.flags,
                            bufferInfo.presentationTimeUs));
                    while (audioRingBuffer.size() > 250) {
                      EncodedFrame removed = audioRingBuffer.removeFirst();
                      audioBufferPool.release(removed.data);
                    }
                  }
                }
                audioEncoder.releaseOutputBuffer(outputIndex, false);
                outputIndex = audioEncoder.dequeueOutputBuffer(bufferInfo, 0);
              }
            } catch (Exception e) {
              Log.e(TAG, "Audio loop error: " + e.getMessage());
            }

            if (isAudioEncoding) {
              backgroundHandler.postDelayed(this, 10);
            }
          }
        });
  }

  private void createCaptureSession() {
    try {
      SurfaceTexture texture = textureView.getSurfaceTexture();
      texture.setDefaultBufferSize(videoWidth, videoHeight);
      Surface previewSurface = new Surface(texture);
      Surface stillSurface = stillImageReader.getSurface();

      List<Surface> surfaces = Arrays.asList(previewSurface, encoderSurface, stillSurface);
      final CaptureRequest.Builder builder =
          cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);
      builder.addTarget(previewSurface);
      builder.addTarget(encoderSurface);

      cameraDevice.createCaptureSession(
          surfaces,
          new CameraCaptureSession.StateCallback() {
            @Override
            public void onConfigured(@NonNull CameraCaptureSession session) {
              captureSession = session;
              try {
                captureSession.setRepeatingRequest(builder.build(), null, backgroundHandler);
                updateStatus("Buffering Video (Ring Buffer Active)");
                appendLog("Camera capture session active. Ring buffer accumulating...");
              } catch (CameraAccessException e) {
                appendLog("Error starting preview: " + e.getMessage());
              }
            }

            @Override
            public void onConfigureFailed(@NonNull CameraCaptureSession session) {
              appendLog("Capture session configuration failed.");
            }
          },
          backgroundHandler);
    } catch (CameraAccessException e) {
      appendLog("CameraAccessException: " + e.getMessage());
    }
  }

  private void triggerMotionPhotoCapture() {
    if (isCapturing || cameraDevice == null || captureSession == null) {
      return;
    }
    isCapturing = true;
    btnShutter.setEnabled(false);
    updateStatus("Capturing Still Photo & Snap Window...");
    appendLog("Shutter pressed! Triggering still capture request...");

    final long shutterVideoPtsUs;
    synchronized (ringBuffer) {
      if (!ringBuffer.isEmpty()) {
        shutterVideoPtsUs = ringBuffer.getLast().ptsUs;
      } else {
        shutterVideoPtsUs = 0;
      }
    }

    try {
      CaptureRequest.Builder captureBuilder =
          cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE);
      captureBuilder.addTarget(stillImageReader.getSurface());

      int rotation = requireActivity().getWindowManager().getDefaultDisplay().getRotation();
      int jpegOrientation = (ORIENTATIONS.get(rotation) + sensorOrientation + 270) % 360;
      captureBuilder.set(CaptureRequest.JPEG_ORIENTATION, jpegOrientation);

      stillImageReader.setOnImageAvailableListener(
          reader -> {
            Image image = reader.acquireLatestImage();
            if (image == null) {
              return;
            }
            appendLog("Still image captured (" + image.getWidth() + "x" + image.getHeight() + ").");

            ByteBuffer buffer = image.getPlanes()[0].getBuffer();
            byte[] jpegBytes = new byte[buffer.remaining()];
            buffer.get(jpegBytes);
            image.close();

            processAndBuildMotionPhoto(jpegBytes, shutterVideoPtsUs, jpegOrientation);
          },
          backgroundHandler);

      captureSession.capture(captureBuilder.build(), null, backgroundHandler);
    } catch (CameraAccessException e) {
      appendLog("Capture error: " + e.getMessage());
      isCapturing = false;
      btnShutter.setEnabled(true);
    }
  }

  private void processAndBuildMotionPhoto(
      byte[] jpegBytes, long shutterVideoPtsUs, int jpegOrientation) {
    appendLog("Collecting post-shutter video context (1.5 seconds)...");
    updateStatus("Collecting 1.5s Post-Shutter Video...");

    // Wait 1.5 seconds to collect future frames into ring buffer
    backgroundHandler.postDelayed(
        () -> {
          try {
            updateStatus("Muxing Video Window & Building Motion Photo...");
            appendLog("Extracting window around shutter from ring buffer...");

            List<EncodedFrame> clipFrames = new ArrayList<>();
            long actualShutterPts = shutterVideoPtsUs;
            synchronized (ringBuffer) {
              if (ringBuffer.isEmpty()) {
                appendLog("Error: Ring buffer is empty.");
                resetCaptureState();
                return;
              }

              if (actualShutterPts <= 0) {
                actualShutterPts = ringBuffer.getLast().ptsUs;
              }

              long targetStartUs = Math.max(0, actualShutterPts - PAST_WINDOW_US);
              long targetEndUs = actualShutterPts + FUTURE_WINDOW_US;

              int keyframeIdx = -1;
              for (int i = 0; i < ringBuffer.size(); i++) {
                EncodedFrame f = ringBuffer.get(i);
                if ((f.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0) {
                  if (f.ptsUs <= targetStartUs || keyframeIdx == -1) {
                    keyframeIdx = i;
                  }
                  if (f.ptsUs >= targetStartUs && keyframeIdx != -1) {
                    break;
                  }
                }
              }

              if (keyframeIdx != -1) {
                for (int i = keyframeIdx; i < ringBuffer.size(); i++) {
                  EncodedFrame f = ringBuffer.get(i);
                  if (f.ptsUs <= targetEndUs) {
                    clipFrames.add(
                        new EncodedFrame(Arrays.copyOf(f.data, f.size), f.size, f.flags, f.ptsUs));
                  }
                }
              }
            }

            if (clipFrames.isEmpty()) {
              appendLog("Error: No valid keyframed clip found in ring buffer.");
              resetCaptureState();
              return;
            }

            List<EncodedFrame> clipAudioFrames = new ArrayList<>();
            synchronized (audioRingBuffer) {
              if (!audioRingBuffer.isEmpty()) {
                int count = Math.min(audioRingBuffer.size(), clipFrames.size() * 2);
                int start = Math.max(0, audioRingBuffer.size() - count);
                for (int i = start; i < audioRingBuffer.size(); i++) {
                  EncodedFrame f = audioRingBuffer.get(i);
                  clipAudioFrames.add(
                      new EncodedFrame(Arrays.copyOf(f.data, f.size), f.size, f.flags, f.ptsUs));
                }
              }
            }

            long firstVideoPtsUs = clipFrames.get(0).ptsUs;
            long presentationTimestampUs = Math.max(0, actualShutterPts - firstVideoPtsUs);
            long clipDurationMs =
                (clipFrames.get(clipFrames.size() - 1).ptsUs - firstVideoPtsUs) / 1000;
            appendLog(
                "Extracted "
                    + clipFrames.size()
                    + " video frames ("
                    + clipDurationMs
                    + " ms, "
                    + clipAudioFrames.size()
                    + " audio frames). Motion Photo Presentation Timestamp: "
                    + presentationTimestampUs
                    + " us.");

            File outputFile =
                new File(
                    requireContext().getExternalFilesDir(null), "captured_motion_photo.MP.jpg");

            // Strategy 1: Mux MP4 video into cache file during capture window
            appendLog("Strategy 1: Muxing exact MP4 trailer into cache file...");
            File tempVideoFile =
                File.createTempFile("motion_vid_cache", ".mp4", requireContext().getCacheDir());
            try (ParcelFileDescriptor pfd =
                ParcelFileDescriptor.open(tempVideoFile, ParcelFileDescriptor.MODE_READ_WRITE)) {
              muxFramesToMp4(clipFrames, clipAudioFrames, pfd.getFileDescriptor(), jpegOrientation);
            }

            // Write primary JPEG image bytes to temp cache file
            File tempImageFile =
                File.createTempFile("motion_img_cache", ".jpg", requireContext().getCacheDir());
            try (FileOutputStream fos = new FileOutputStream(tempImageFile)) {
              fos.write(jpegBytes);
            }

            appendLog(
                "Strategy 1: Exact MP4 trailer completed ("
                    + tempVideoFile.length()
                    + " bytes). Assembling Motion Photo in single pass via"
                    + " MotionPhotoBuilderService...");

            int result = -1;
            if (isBuilderBound && builderService != null) {
              result =
                  builderService.buildMotionPhoto(
                      tempImageFile.getAbsolutePath(),
                      tempVideoFile.getAbsolutePath(),
                      null,
                      null,
                      presentationTimestampUs,
                      outputFile.getAbsolutePath());
            } else {
              appendLog("MotionPhotoBuilderService not bound, cannot assemble motion photo.");
            }

            tempImageFile.delete();
            tempVideoFile.delete();

            if (result == 0 && outputFile.exists() && outputFile.length() > jpegBytes.length) {
              appendLog("SUCCESS! Strategy 1 Motion Photo saved: " + outputFile.getAbsolutePath());
              viewModel.setBuiltMotionPhotoFile(outputFile);
              prepareResultPreview(requireContext(), outputFile);
            } else {
              appendLog("FAILED to create Strategy 1 Motion Photo (code " + result + ").");
            }

          } catch (Exception e) {
            appendLog("Error in processing pipeline: " + e.getMessage());
          } finally {
            resetCaptureState();
          }
        },
        1500);
  }

  private void muxFramesToMp4(
      List<EncodedFrame> videoFrames,
      List<EncodedFrame> audioFrames,
      FileDescriptor fd,
      int jpegOrientation)
      throws IOException {
    if (videoFrames.isEmpty() && audioFrames.isEmpty()) {
      return;
    }

    MediaMuxer muxer = new MediaMuxer(fd, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
    muxer.setOrientationHint(jpegOrientation);

    MediaFormat videoFormat =
        MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_HEVC, videoWidth, videoHeight);
    if (csd0 != null) {
      videoFormat.setByteBuffer("csd-0", csd0);
    }
    int videoTrackIndex = muxer.addTrack(videoFormat);

    int audioTrackIndex = -1;
    if (!audioFrames.isEmpty() && audioCsd0 != null) {
      MediaFormat audioFormat =
          MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, 44100, 1);
      audioFormat.setByteBuffer("csd-0", audioCsd0);
      audioTrackIndex = muxer.addTrack(audioFormat);
    }

    muxer.start();

    MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
    long firstVideoPts = -1;
    long lastVideoPts = -1;
    for (EncodedFrame frame : videoFrames) {
      if (firstVideoPts == -1) {
        firstVideoPts = frame.ptsUs;
      }
      long pts = Math.max(0, frame.ptsUs - firstVideoPts);
      if (pts <= lastVideoPts) {
        pts = lastVideoPts + 1000L;
      }
      lastVideoPts = pts;

      ByteBuffer buffer = ByteBuffer.wrap(frame.data, 0, frame.size);
      info.set(0, frame.size, pts, frame.flags);
      muxer.writeSampleData(videoTrackIndex, buffer, info);
    }

    if (audioTrackIndex != -1) {
      long firstAudioPts = -1;
      long lastAudioPts = -1;
      for (EncodedFrame frame : audioFrames) {
        if (firstAudioPts == -1) {
          firstAudioPts = frame.ptsUs;
        }
        long pts = Math.max(0, frame.ptsUs - firstAudioPts);
        if (pts <= lastAudioPts) {
          pts = lastAudioPts + 1000L;
        }
        lastAudioPts = pts;
        ByteBuffer buffer = ByteBuffer.wrap(frame.data, 0, frame.size);
        info.set(0, frame.size, pts, frame.flags);
        muxer.writeSampleData(audioTrackIndex, buffer, info);
      }
    }

    muxer.stop();
    muxer.release();
    appendLog("Muxed HEVC video + AAC audio stream over file descriptor.");
  }

  private void prepareResultPreview(Context context, File motionPhotoFile) {
    File extractedVideoFile;
    try {
      extractedVideoFile = File.createTempFile("extracted_result", ".mp4", context.getCacheDir());
    } catch (IOException e) {
      appendLog("Failed to create temp video file for preview: " + e.getMessage());
      return;
    }
    File tempImageFile = new File(context.getCacheDir(), "extracted_temp_image.jpg");
    File tempMetadataFile = new File(context.getCacheDir(), "extracted_temp_metadata.xml");

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
        appendLog("Error calling extractMotionPhoto: " + e.getMessage());
      }
    } else {
      appendLog("MotionPhotoExtractorService not bound, cannot extract preview.");
    }

    if (extractResult == 0 && tempImageFile.exists()) {
      viewModel.setExtractedVideoFile(extractedVideoFile);
      viewModel.setImageUri(Uri.fromFile(tempImageFile));
      appendLog("Successfully extracted Motion Photo! Navigating to Result Preview...");
      if (getActivity() != null) {
        getActivity()
            .runOnUiThread(
                () -> {
                  if (getActivity() instanceof MainActivity) {
                    ((MainActivity) getActivity()).navigateToCreatorResult();
                  }
                });
      }
    } else {
      appendLog(
          "Failed to extract preview from captured Motion Photo (code " + extractResult + ").");
    }
  }

  private void resetCaptureState() {
    isCapturing = false;
    if (getActivity() != null) {
      getActivity()
          .runOnUiThread(
              () -> {
                btnShutter.setEnabled(true);
                updateStatus("Buffering Video (Ring Buffer Active)");
              });
    }
  }

  private void updateStatus(String status) {
    if (getActivity() != null) {
      getActivity().runOnUiThread(() -> tvBufferStatus.setText("Status: " + status));
    }
  }

  private void appendLog(String log) {
    Log.d(TAG, log);
    if (getActivity() != null) {
      getActivity()
          .runOnUiThread(
              () -> {
                tvCaptureLogs.append(log + "\n");
                svLogs.fullScroll(View.FOCUS_DOWN);
              });
    }
  }

  private void startBackgroundThread() {
    backgroundThread = new HandlerThread("CameraBackground");
    backgroundThread.start();
    backgroundHandler = new Handler(backgroundThread.getLooper());
  }

  private void stopBackgroundThread() {
    if (backgroundThread != null) {
      backgroundThread.quitSafely();
      try {
        backgroundThread.join();
        backgroundThread = null;
        backgroundHandler = null;
      } catch (InterruptedException e) {
        Log.e(TAG, "Background thread interrupted: " + e.getMessage());
      }
    }
  }

  private void closeCamera() {
    isEncoding = false;
    isAudioEncoding = false;
    if (captureSession != null) {
      captureSession.close();
      captureSession = null;
    }
    if (cameraDevice != null) {
      cameraDevice.close();
      cameraDevice = null;
    }
    if (videoEncoder != null) {
      videoEncoder.stop();
      videoEncoder.release();
      videoEncoder = null;
    }
    if (audioEncoder != null) {
      audioEncoder.stop();
      audioEncoder.release();
      audioEncoder = null;
    }
    if (audioRecord != null) {
      audioRecord.stop();
      audioRecord.release();
      audioRecord = null;
    }
    if (stillImageReader != null) {
      stillImageReader.close();
      stillImageReader = null;
    }
  }
}
