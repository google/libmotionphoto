# Motion Photo Sample Application

This project is a sample Android application demonstrating the usage of `libmotionphoto` library to build, validate, and extract Google Motion Photos, with support for 3P formats (via C++ Metadata Engine and plugins).

## Prerequisites

To build the project, you need:

- Android SDK (installed with Android Studio)
- Android NDK (version 25.1.8937393 or compatible)
- CMake (version 3.22.1 or compatible)

## Build Instructions

Before building the Android app, you must build the pre-requisite native dependencies (Abseil and Protobuf) for Android.

### 1. Build Native Dependencies

Set the `ANDROID_NDK_HOME` environment variable to your Android NDK path, and run the dependency build script (relative to this directory):

```bash
export ANDROID_NDK_HOME=/path/to/your/Android/Sdk/ndk/25.1.8937393
../../external/build_deps_android.sh
```

This will build Abseil and Protobuf for `arm64-v8a` and install them under `external/install_arm64-v8a/`.

### 2. Build the Android App

You can build the app using Gradle from the command line or import the `android/` directory into Android Studio.

To build from the command line (from the `android/` directory):

```bash
cd ..
./gradlew assembleDebug
```

This will compile the JNI library (using the prebuilt dependencies) and package the APK under `android/app/build/outputs/apk/debug/app-debug.apk`.

### 3. Install the App

Install the APK to your connected device using `adb`:

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

## Running the Demo

The app contains three main workflows:

1.  **Capture Demo**: Demonstrates live simultaneous camera capture with
    dual-surface rendering (TextureView preview + MediaCodec HEVC video
    encoder + AudioRecord AAC audio encoder). Maintains a pre-allocated reusable
    memory buffer pool (`ByteArrayPool`) to buffer $[T-3\text{s}, T+3\text{s}]$
    video and audio packets with zero GC allocation during continuous capture.
2.  **Creator Flow**: Allows you to combine a JPEG photo and an MP4 video into a
    valid Google Motion Photo. You can select your own files or use the "Use
    Default" buttons (which use assets packaged in the app).
3.  **Extractor Flow**: Allows you to parse and extract files from a Motion
    Photo.
    *   **3P Support Switch**: Turn "Disable 3P Plugins" OFF to enable the `Example.Oem` plugin, which detects and extracts video from 3P format files (e.g. `sample.jpg` renamed to `example.oem`).
    *   Clicking on parsed metadata blocks in the "Metadata" tab will show the decoded JSON representation of the block payload.
