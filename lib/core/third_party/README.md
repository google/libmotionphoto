# Third-Party Dependencies and Security Maintenance Policy

`libmotionphoto` supports flexible, modern third-party dependency resolution:
1. **System Detection**: Automatically detects pre-installed system packages (`libheif-dev`, `libmp4v2-dev`, `modp_b64`) via CMake config packages, `pkg-config`, or standard library finders.
2. **On-Demand Dynamic Fetching**: If system libraries are not found, CMake automatically downloads and builds the dependencies at configure time using `FetchContent` (e.g. from upstream GitHub repositories).
3. **Local Vendored Fallback**: Supports local source directories in `sources/third_party/` for offline or embedded build workflows.

## Dependencies

Dependency   | Version / Revision | Purpose                                            | Upstream Source
:----------- | :----------------- | :------------------------------------------------- | :--------------
**libheif**  | v1.17.6+           | HEIF/HEIC container and image structure parsing    | [GitHub repo](https://github.com/strukturag/libheif)
**mp4v2**    | v2.0.0+            | MP4 box layout reading and validation              | [GitHub repo](https://github.com/techsmith/mp4v2)
**modp_b64** | v3.10.3+           | Fast Base64 encoding and decoding                  | [GitHub repo](https://github.com/client9/stringencoders)
**image_io** | Internal Snapshot  | Stream memory buffering and data source management | Internal Google library

## CMake Configuration Flags

Option | Default | Description
:----- | :------ | :----------
`LIBMOTIONPHOTO_USE_SYSTEM_DEPS` | `ON` | Attempt to find and link against system-installed libraries first.
`LIBMOTIONPHOTO_DOWNLOAD_DEPS`   | `ON` | Download missing dependencies from upstream Git repositories via `FetchContent`.
`USE_SYSTEM_LIBHEIF`            | `ON` | Look for system-installed `libheif` (`libheif-dev` on Debian/Ubuntu, `brew install libheif` on macOS).
`USE_SYSTEM_MP4V2`              | `ON` | Look for system-installed `mp4v2` (`libmp4v2-dev` on Debian/Ubuntu, `brew install mp4v2` on macOS).
`USE_SYSTEM_MODP_B64`           | `ON` | Look for system-installed `modp_b64`.

## Security Patching & Maintenance Policy

1.  **Continuous Fuzzing**: Third-party parser libraries (especially `libheif`)
    must be maintained with continuous fuzzing enabled (via OSS-Fuzz /
    ClusterFuzz).
2.  **Upstream Vulnerability Monitoring**: Security advisories (CVEs) for
    `libheif` and related media codecs are monitored continuously.
3.  **Patch Sync Cadence**:
    -   Critical / High severity security patches (e.g. out-of-bounds
        reads/writes in media parsers) are cherry-picked immediately.
    -   Regular updates are synchronized semi-annually or upon upstream stable
        releases.
