# Motion Photo Java API Description

This document describes the Java API wrapper for the native Motion Photo C++
library. The API allows Android applications to parse, validate, build, and
extract Google Motion Photos and their associated dynamic metadata (such as
SMPTE ST 2094-50 / AGTM HDR tone mapping).

## Architecture Overview

The Java layer provides high-level, type-safe facades around the native C++
library (`libmotion_photo_jni.so`). Native method declarations and JNI bridging
are encapsulated within package-private JNI classes (`MetadataEngineJni`,
`MotionPhotoJni`), while public consumers interact exclusively with public
facades or isolated Android services.

```mermaid
graph TD
    App[Android App / Playground UI] --> VM[PlaygroundViewModel]
    App --> Storage[StorageUtils]
    App --> Service[MetadataParserService <br/><i>(Isolated Process Sandboxing)</i>]
    App --> Engine[MetadataEngine]
    App --> Builder[MotionPhotoBuilder]
    App --> Extractor[MotionPhotoExtractor]
    App --> Checker[MotionPhotoChecker]

    Service --> Engine
    Engine --> JNI_Meta[MetadataEngineJni <br/><i>(package-private)</i>]
    Builder --> JNI_Photo[MotionPhotoJni <br/><i>(package-private)</i>]
    Extractor --> JNI_Photo
    Checker --> JNI_Photo

    JNI_Meta --> Native[libmotion_photo_jni.so]
    JNI_Photo --> Native

    Native --> C_Meta[C++ MetadataEngine / libmotionphoto_api]
    Native --> C_Builder[C++ MotionPhotoBuilder]
    Native --> C_Extractor[C++ MotionPhotoExtractor]
    Native --> C_Checker[C++ MotionPhotoChecker]
```

## JNI Layer Encapsulation

The low-level JNI interface classes (`MetadataEngineJni` and `MotionPhotoJni`)
are **package-private** to `com.google.libmotionphoto.motionphoto`. All native
methods are non-public and load `libmotion_photo_jni.so`. External callers
should always use the public facade classes described below.

### Output Callback

```java
public interface OutputCallback {
  void onOutput(String message);
}
```

Used to receive logging and status messages from the native library. The
callback runs synchronously on the calling thread (typically a background worker
thread).

--------------------------------------------------------------------------------

## API Components

### 1. `MetadataEngine`

The primary public entry point for extracting and inspecting Motion Photo
metadata, container structures, and dynamic tone mapping metadata without
invoking full media extraction pipelines.

#### Methods

```java
// Parsing & Container Metadata
public static byte[] parseMetadata(String filePath)
public static byte[] parseMetadataFd(int fd)
public static byte[] parseMetadataFd(int fd, long offset, long length)

// Motion Photo Validation
public static boolean isMotionPhoto(byte[] serializedCollection)
public static boolean isMotionPhoto(
    byte[] serializedCollection, boolean disable3pPlugins, String[] enabled3pPlugins)

// AGTM / SMPTE ST 2094-50 Dynamic Metadata Extraction (Automatic Timestamp)
public static String extractAgtm(String filePath)
public static String extractAgtm(byte[] data)
public static String extractAgtmFd(int fd)
public static String extractAgtmFd(int fd, long offset, long length)

// AGTM Extraction at Explicit Timestamp
public static String extractAgtmAtTimestamp(String filePath, long timestampUs)

// Raw Metadata Extraction
public static boolean extractMetadata(String inputFilePath, String outputMetadataFilePath)
```

#### Method Descriptions

*   **`parseMetadata` / `parseMetadataFd`**: Parses the image/container headers
    (JPEG, HEIC, MP4) and returns serialized `MetadataCollection` proto bytes
    containing primary image dimensions, container items, and XMP metadata.
    Supports POSIX file descriptors and sub-slice offsets for APK asset streams.
*   **`isMotionPhoto`**: Evaluates serialized `MetadataCollection` proto bytes to
    determine whether the container constitutes a valid Motion Photo. Supports
    optional third-party plugin enablement/filtering.
*   **`extractAgtm` / `extractAgtmFd`**: Extracts SMPTE ST 2094-50 (AGTM) HDR
    tone mapping metadata from a Motion Photo file, in-memory byte buffer, or
    POSIX file descriptor. Automatically parses the container metadata first to
    extract the primary image's target presentation timestamp without requiring
    manual timestamp parameters (matching the C++ layer's `ExtractAgtmFromFile`
    and `ExtractAgtmFromMemory` APIs).
*   **`extractAgtmAtTimestamp`**: Extracts SMPTE ST 2094-50 (AGTM) metadata
    corresponding to an explicit presentation timestamp (in microseconds) from
    the embedded video track of the specified file path.
*   **`extractMetadata`**: Extracts raw embedded XMP/container metadata from the
    input image and writes it directly to the specified output file path.

--------------------------------------------------------------------------------

### 2. `MotionPhotoBuilder`

Used to assemble a Motion Photo by combining a primary image and a primary video
(with optional alternate moments and XMP metadata).

#### Methods

```java
public static int build(Params params, MotionPhotoJni.OutputCallback callback)

public static int buildFromMemory(
    byte[] imageBytes,
    byte[] videoBytes,
    String momentsXmp,
    String outputImageFile,
    MotionPhotoJni.OutputCallback callback)
```

#### Parameters: `MotionPhotoBuilder.Params`

| Field Name | Type | Description | Required/Optional |
| :--- | :--- | :--- | :--- |
| `primaryImage` | `String` | Path or File Descriptor path (`/proc/self/fd/N`) to primary image (JPEG or HEIC). | Required |
| `primaryVideo` | `String` | Path or File Descriptor path (`/proc/self/fd/N`) to primary video (MP4). | Required |
| `momentsVideo` | `String` | Path to alternate moments video file. | Optional |
| `momentsXmp` | `String` | Path to XMP metadata file for moments. | Optional |
| `outputImage` | `String` | Output Motion Photo destination file path. | Required |
| `workingVideo` | `String` | Working video path (defaults to `/tmp/motion_photo_working_trailer.mp4`). | Optional |
| `optimizedVideo`| `String` | Optimized video path (defaults to `/tmp/motion_photo_optimized_trailer.mp4`). | Optional |
| `presentationTimestampUs` | `long` | Primary photo presentation timestamp in video (defaults to `-1`). | Optional |

*Note: All file path parameters support standard filesystem paths as well as
Linux process file descriptors (`/proc/self/fd/N`) for in-memory streaming.*

#### Single-Pass In-Memory Method (`buildFromMemory`)

Directly combines in-memory primary image bytes and finalized MP4 video bytes
into a Motion Photo container in a single pass with exact XMP video length
metadata, eliminating upfront length estimation and secondary header rewriting.

#### Return Value

*   `0`: Success.
*   Non-zero: Error occurred during the build process.

--------------------------------------------------------------------------------

### 3. `MotionPhotoExtractor`

Used to extract the individual assets (primary image, video, and XMP metadata)
from an existing Motion Photo container.

#### Method

```java
public static int extract(Params params, MotionPhotoJni.OutputCallback callback)
```

#### Parameters: `MotionPhotoExtractor.Params`

| Field Name | Type | Description | Required/Optional |
| :--- | :--- | :--- | :--- |
| `motionPhotoFile` | `String` | Path to the input Motion Photo file. | Required |
| `primaryImageOutput`| `String` | Output path for the extracted primary image. | Optional* |
| `videoOutput` | `String` | Output path for the extracted video. | Optional* |
| `metadataOutput` | `String` | Output path for the extracted XMP metadata (XML). | Optional* |

*\*Note: At least one of `primaryImageOutput`, `videoOutput`, or
`metadataOutput` must be provided.*

#### Return Value

*   `0`: Success.
*   Non-zero: Error occurred during extraction.

--------------------------------------------------------------------------------

### 4. `MotionPhotoChecker`

Validates whether a target file is a compliant Google Motion Photo conforming to
the specification.

#### Method

```java
public static int check(
    String motionPhotoFile, MotionPhotoJni.OutputCallback callback)
```

#### Parameters

*   `motionPhotoFile` (`String`): Path to the candidate file.
*   `callback` (`MotionPhotoJni.OutputCallback`): Callback for validation logs.

#### Return Value

*   `0`: Valid Motion Photo (no errors).
*   Non-zero: Invalid Motion Photo or verification failed.

--------------------------------------------------------------------------------

### 5. Sandboxed Android Services (Isolated Process Architecture)

For secure, out-of-process media ingestion and creation, libmotionphoto provides four dedicated Android services configured with `android:isolatedProcess="true"`:

1. **`MetadataParserService`** (`IMetadataParserService.aidl`):
   Parses container metadata and extracts SMPTE ST 2094-50 (AGTM) HDR tone mapping metadata.
   ```java
   interface IMetadataParserService {
       byte[] parseMetadata(in ParcelFileDescriptor pfd);
       boolean isMotionPhoto(
           in byte[] serializedCollection,
           boolean disable3pPlugins,
           in List<String> enabled3pPlugins);
       String extractAgtm(in ParcelFileDescriptor pfd);
   }
   ```

2. **`MotionPhotoBuilderService`** (`IMotionPhotoBuilderService.aidl`):
   Assembles Motion Photo containers from files or in-memory byte buffers.
   ```java
   interface IMotionPhotoBuilderService {
       int buildMotionPhoto(
           in String imagePath,
           in String videoPath,
           in String momentsVideoPath,
           in String momentsXmp,
           long presentationTimestampUs,
           in String outputImagePath);

       int buildMotionPhotoFromMemory(
           in byte[] imageBytes,
           in byte[] videoBytes,
           in String momentsXmp,
           in String outputImagePath);
   }
   ```

3. **`MotionPhotoExtractorService`** (`IMotionPhotoExtractorService.aidl`):
   Extracts primary image, video track, and metadata XML streams from motion photos.
   ```java
   interface IMotionPhotoExtractorService {
       int extractMotionPhoto(
           in String motionPhotoFile,
           in String primaryImageOutput,
           in String videoOutput,
           in String metadataOutput);
   }
   ```

4. **`MotionPhotoCheckerService`** (`IMotionPhotoCheckerService.aidl`):
   Validates compliance of candidate Motion Photo files.
   ```java
   interface IMotionPhotoCheckerService {
       int checkMotionPhoto(in String motionPhotoFile);
   }
   ```

--------------------------------------------------------------------------------

## Threading, Performance, and Security

*   **Blocking Calls**: All Java API calls (`build`, `extract`, `check`,
    `MetadataEngine.*`) perform direct I/O and native computations, blocking the
    calling thread. **Never invoke these methods on the Android Main (UI)
    Thread.**
*   **Asynchronous Usage**: In the reference sample app, operations are
    dispatched across background thread pool executors (`ExecutorService`), and
    results are posted back via `LiveData` / state flows.
*   **Process Isolation**: For untrusted media ingestion, always bind to the
    sandboxed services (`MetadataParserService`, `MotionPhotoBuilderService`,
    `MotionPhotoExtractorService`, `MotionPhotoCheckerService`) across AIDL to
    guarantee memory safety boundaries outside the main application heap.

