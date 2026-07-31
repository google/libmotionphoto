# libmotionphoto

> **Disclaimer**: This is not an officially supported Google product. This project is not eligible for the [Google Open Source Software Vulnerability Rewards Program](https://bughunters.google.com/open-source-security).

`libmotionphoto` is a cross-platform C++ and Java library for parsing, building, validating, and extracting Google Motion Photos across JPEG and HEIF (HEIC/AVIF) image containers alongside MP4 video trailers.

## Architecture & Features

* **C++ Core Libraries**:
  * `motion_photo`: Core metadata structures, XMP parser, and provider interfaces.
  * `motion_photo_jpeg`: APP1 segment parsing and JPEG Motion Photo processing.
  * `motion_photo_heif`: MPVD box handling and HEIF/HEIC/AVIF container support.
  * `motion_photo_mp4`: Video trailer parsing and track optimization.
*   **Plugins & Format Extensibility (`lib/core/plugins`)**: Modular Motion Photo
    Provider Interface (`IMetadataProvider`) plugins for identifying, parsing,
    and extracting proprietary or legacy motion photo formats (such as legacy
    Pixel `GCamera:MicroVideo` formats). See
    [Plugin Integration Guidance](lib/core/plugins/legacy_micro_video/README.md) for
    details on authoring and registering plugins.
*   **Executables**:
    * `motion_photo_builder`: Builds a Motion Photo from primary image, video, and XMP metadata.
    * `motion_photo_checker`: Validates Motion Photo compliance against specifications.
    * `motion_photo_extractor`: Extracts primary image, video trailer, and metadata blocks.
    *   `motion_photo_metadata_engine`: Extracts semantic metadata and outputs
        structured JSON or Protobuf representations.
* **Java & JNI Wrappers**: High-level Java APIs (`MetadataEngine`, `MotionPhotoBuilder`, `MotionPhotoExtractor`, `MotionPhotoChecker`) bridging native execution via JNI.
* **Android Sample App**: Sample application demonstrating mobile integration under `samples/android`.

## Developer Guide & API Usage

### C++ API (`libmotionphoto_api.h`)

[`libmotionphoto_api.h`](libmotionphoto_api.h) is the sole public C++ facade
header, providing clean C++17 interfaces (`<cstdint>`, `<string>`, `<vector>`)
with zero internal engine leakage.

<details>
<summary><b>View C++ Usage Example & Build Instructions</b></summary>

#### C++ Code Example

```cpp
#include "libmotionphoto_api.h"

// 1. Inspect container & metadata
libmotionphoto::api::MotionPhotoInfo info =
    libmotionphoto::api::ParseMotionPhotoFromFile("/path/to/IMG_MP.jpg");
if (info.is_motion_photo) {
  int64_t video_offset = info.video_offset;
  int64_t video_length = info.video_length;
}

// 2. Validate compliance
bool is_valid = libmotionphoto::api::CheckMotionPhotoFile("/path/to/IMG_MP.jpg");

// 3. Extract embedded MP4 video track
bool extracted = libmotionphoto::api::ExtractVideoTrackFromFile(
    "/path/to/IMG_MP.jpg", "/path/to/extracted_video.mp4");

// 4. Extract SMPTE ST 2094-50 (AGTM HDR) dynamic tone mapping metadata
std::string agtm_json = libmotionphoto::api::ExtractAgtmFromFile("/path/to/IMG_MP.jpg");

// 5. Author a new Motion Photo container
libmotionphoto::api::MotionPhotoBuilderParams params;
params.primary_image_file_name = "/path/to/still.jpg";
params.primary_video_file_name = "/path/to/trailer.mp4";
params.output_file_name = "/path/to/IMG_MP.jpg";
params.presentation_timestamp_us = 500000;
bool built = libmotionphoto::api::BuildMotionPhotoFromFile(params);
```

#### Building / Linking in C++

*   **CMake**:

    ```cmake
    target_link_libraries(your_app PRIVATE motion_photo_api)
    ```
*   **Bazel / Google3**:

    ```python
    deps = ["//path/to/libmotionphoto:libmotionphoto_api"]
    ```

</details>

--------------------------------------------------------------------------------

### Java / Android API (`:libmotionphoto`)

High-level Java SDK classes are located in package
`com.google.libmotionphoto.motionphoto`.

<details>
<summary><b>View Java Usage Example & Build Instructions</b></summary>

#### Java Code Example

```java
import com.google.libmotionphoto.motionphoto.MetadataEngine;
import com.google.libmotionphoto.motionphoto.MotionPhotoExtractor;
import com.google.libmotionphoto.motionphoto.MotionPhotoBuilder;
import com.google.libmotionphoto.motionphoto.MotionPhotoChecker;

// 1. Parse metadata / Extract AGTM HDR metadata
byte[] metadataProto = MetadataEngine.parseMetadata(filePath);
String agtmJson = MetadataEngine.extractAgtm(filePath);

// 2. Extract media streams
boolean ok = MotionPhotoExtractor.extract(
    inputPath, outputImagePath, outputVideoPath, outputXmpPath);

// 3. Build Motion Photo
MotionPhotoBuilder.BuilderParams params = new MotionPhotoBuilder.BuilderParams();
params.primaryImageFilePath = stillImagePath;
params.primaryVideoFilePath = videoFilePath;
params.outputFilePath = outputPath;
boolean success = MotionPhotoBuilder.build(params);

// 4. Validate Motion Photo
boolean valid = MotionPhotoChecker.check(filePath);
```

#### Sandboxed Service Integration (Android)

For secure untrusted media handling, bind to `MetadataParserService`
(`android:isolatedProcess="true"`) via AIDL using `ParcelFileDescriptor`
streaming:

```java
ParcelFileDescriptor pfd = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
String agtmJson = metadataParserService.extractAgtm(pfd);
```

#### Building Java / Android

In your `build.gradle`:

```groovy
dependencies {
    implementation project(':libmotionphoto') // or AAR artifact
}
```

</details>

## Security & Sandbox Isolation Architecture

`libmotionphoto` includes a multi-layered security architecture designed for
mobile environments. Because media containers (JPEG, HEIF, AVIF, MP4) often
originate from untrusted external sources, native C++ parsing and decoding are
strictly decoupled and sandboxed from the host application process.

<details>
<summary><b>View Process & IPC Isolation Architecture Diagram</b></summary>

```text
╭─────────────────────────────────────────────────────────────────────────────────────────╮
│                                 Host Application Process                                │
│                                                                                         │
│  ╭───────────────────────────────────────────────────────────────────────────────────╮  │
│  │                    Application UI / Activities / ViewModels                       │  │
│  ╰─────────────────────────────────────────┬─────────────────────────────────────────╯  │
│                                            ▼                                            │
│  ╭───────────────────────────────────────────────────────────────────────────────────╮  │
│  │                    High-Level Java SDK Layer (Public APIs)                        │  │
│  │   [MetadataEngine]    [MotionPhotoExtractor]   [MotionPhotoBuilder]   [Checker]   │  │
│  ╰─────────────────────────────────────────┬─────────────────────────────────────────╯  │
╰────────────────────────────────────────────┼────────────────────────────────────────────╯
                                             ▼
 ═══════════════════════════════ Android Binder IPC Boundary ══════════════════════════════
    [IMetadataParserService]      [IMotionPhotoExtractorService]     [IMotionPhotoBuilderService]
         │ (ParcelFileDescriptor Streaming)                          │ (Filesystem Path Isolation)
         ▼                                                           ▼
╭─────────────────────────────────────╮   ╭───────────────────────────────────────────────╮
│        MetadataParserService        │   │           Dedicated Worker Services           │
│   [android:isolatedProcess="true"]  │   │     (Extractor, Builder, Checker Services)    │
│                                     │   │              [android:exported="false"]       │
│  • Isolated Linux UID (u0_iXX)      │   │                                               │
│  • Zero filesystem / network access │   │  • Asynchronous background worker execution   │
│  • Strict SELinux isolated_app      │   │  • Encapsulates native C++ file operations    │
│  • Encapsulated JNI C++ bindings    │   │  • Offloads heavy memory & I/O from UI thread │
╰──────────────────┬──────────────────╯   ╰───────────────────────┬───────────────────────╯
                   │                                              │
                   ╰───────────────────────┬──────────────────────╯
                                           ▼
╭─────────────────────────────────────────────────────────────────────────────────────────╮
│                                 Native C++ Core Engines                                 │
│                                                                                         │
│   ╭────────────────────────────╮  ╭────────────────────────────╮  ╭─────────────────╮   │
│   │    libmotionphoto_api      │  │    motion_photo_jpeg /     │  │ motion_photo_mp4│   │
│   │   (High-level C++ facade)  │  │    motion_photo_heif       │  │ (MP4 box parser)│   │
│   ╰────────────────────────────╯  ╰────────────────────────────╯  ╰─────────────────╯   │
│                                                                                         │
│   ╭─────────────────────────────────────────────────────────────────────────────────╮   │
│   │    ImageIO / Memory-Safe Provider Registry (IMetadataProvider Plugins)          │   │
│   ╰─────────────────────────────────────────────────────────────────────────────────╯   │
╰────────────────────────────────────────────┬────────────────────────────────────────────╯
                                             ▼
                              🛡️ [Crash Containment Boundary]
       If malformed untrusted media triggers a fault/SIGSEGV, only the isolated service
       process terminates. The host application catches DeadObjectException, auto-recovers,
       and handles the error gracefully without crashing.
```

### Key Security Properties

1.  **Isolated Process Sandbox (`android:isolatedProcess="true"`)**:
    `MetadataParserService` executes inside Android's `isolated_app` SELinux
    domain under an ephemeral UID with zero ambient filesystem, IPC, or network
    permissions.
2.  **File Descriptor Handle Passing**: File contents are shared exclusively via
    Binder `ParcelFileDescriptor` handles (`pread` random-access read calls).
3.  **Crash Containment**: Untrusted or corrupted media streams that might
    trigger memory violations in native parsing code are contained inside the
    isolated service process (`DeadObjectException` auto-recovery).
4.  **Thread & Memory Decoupling**: Path-based builder, checker, and extractor
    services run in dedicated internal background services
    (`android:exported="false"`), preventing UI thread stalls.

</details>

## Building and Testing

### Prerequisites

*   C++17 compiler (`gcc`, `clang`)
* `cmake` (VERSION 3.14+)
*   `protobuf` and `abseil-cpp`

### Quickstart (Linux & macOS)

```bash
./build.sh   # Build C++ libraries and CLI executables
./test.sh    # Run 54 unit tests
```

<details>
<summary><b>View Repository Structure Tree</b></summary>

```text
libmotionphoto/
├── libmotionphoto_api.h    # Sole public C++ facade header
├── CMakeLists.txt          # Top-level CMake configuration
├── build.sh                # Top-level CMake build runner
├── test.sh                 # Unit test execution runner
├── LICENSE
├── README.md
├── CONTRIBUTING.md
├── .github/
│   └── workflows/          # GitHub Actions CI for Linux & macOS
├── lib/                    # Decoupled core libraries
│   ├── core/               # Native C++ engine (internal headers, plugins, third_party, and tests)
│   │   ├── CMakeLists.txt
│   │   ├── motion_photo/   # Core C++ engine sources, private headers, and unit tests
│   │   ├── plugins/        # Format provider plugins (e.g. legacy_micro_video)
│   │   ├── third_party/    # Vendored C++ dependencies (image_io, mp4v2)
│   │   └── testdata/       # Test assets
│   ├── java/               # Pure JVM Java library module
│   │   ├── build.gradle
│   │   ├── java_api_description.md # Complete Java API documentation
│   │   └── src/main/java/com/google/libmotionphoto/motionphoto/
│   └── android/            # Android IPC & AIDL sandbox module
│       ├── build.gradle
│       └── src/main/
│           ├── aidl/com/google/libmotionphoto/ # Promoted AIDL service interfaces
│           └── java/com/google/libmotionphoto/ # Promoted Android sandboxed services
├── javatests/              # Java unit test suite
├── samples/
│   └── android/            # Android sample playground app (:libmotionphoto)
└── tools/                  # Developer tools and CI scripts
```

</details>

## Contributing

*   **General Contributions**: See [CONTRIBUTING.md](CONTRIBUTING.md) for
    guidelines on contributing to this project and signing the Contributor
    License Agreement (CLA).
*   **Format Plugin Contributions**: If you would like to contribute support for
    additional legacy or proprietary motion photo formats, please follow our
    [Plugin Contribution Guide](lib/core/plugins/legacy_micro_video/README.md) for
    step-by-step instructions on implementing provider interfaces, defining
    metadata schemas, and adding unit tests.

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.
