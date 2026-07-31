Photos Formats Motion Photo Package

This package provides libraries for the representation of the image and video
metadata associated with a motion photo, and the means via the image_io libraries
to read and write the metadata.

This package's include and src directories have the same subdirectory structure:

*   motion_photo
    *   The C++ classes that represent the motion photo image and video
        metadata.
    *   This code depends on the image_io/base, image_io/iso, image_io/xml and
        image_io_xmp libraries.
*   motion_photo_jpeg
    *   The C++ classes that can be used to get motion photo metadata from a
        JPEG file.
    *   This code depends on the motion_photo and its dependencies, and on the
        image_io/jpeg library.
*   motion_photo_mp4
    *   The C++ classes that can be used to get motion photo metadata from an
        MP4 file.
    *   This code depends on the motion_photo and its dependencies, and on the
        //third_party/mp4v2 library.
*   motion_photo_heic
    *   The C++ classes that can be used to get motion photo metadata from an
        HEIC file.
    *   This code depends on the motion_photo and its dependencies, and on the
        //third_party/libheif library.
*   motion_photo_checker_extractor_common (Internal Usage Only)
    *   Shared logic for checker and extractor, including helper functions for
        file I/O, parsing XMP metadata, and reading MP4 video details. This
        package is internal and should only be accessed by motion_photo_checker
        and motion_photo_extractor.
    *   This code depends on the motion_photo, motion_photo_jpeg,
        motion_photo_heic, and motion_photo_mp4 libraries.
*   motion_photo_checker
    *   This program runs on linux and macos systems. It accepts a motion photo
        file on the command line and checks the contents to makes sure it
        conforms to the specification.
    *   This code depends on the motion_photo_checker_extractor_common library
        and its dependencies.
*   motion_photo_builder
    *   This program runs on linux and macos systems. It accepts an image file,
        one or more video files and some addtional metadata in JSON format to
        produce a JPEG file that houses a motion photo.
    *   This code depends on the motion_photo, motion_photo_image_metadata_io
        libraries and their dependencies, as well as //third_party/mp4v2.
*   motion_photo_extractor
    *   This program runs on linux and macos systems. It accepts a motion photo
        file on the command line and splits it to its image portion, video
        portion and metadata.
    *   This code depends on the motion_photo_checker_extractor_common library
        and its dependencies.
*   motion_photo_jni
    *   JNI bindings mapping Java calls to C++ implementations for building,
        checking, and extracting motion photos.
    *   This code depends on motion_photo_builder, motion_photo_checker,
        motion_photo_extractor, and their dependencies.

Other layers in the motion_photo_sample_src workspace:

*   Java API Wrappers (located in ../../java/)
    *   Provides Java classes (MotionPhotoBuilder, MotionPhotoExtractor,
        MotionPhotoChecker) that interface with JNI.
    *   Detailed documentation is available in
        ../../java/java_api_description.md.
    *   Note: Any changes to the Java API must be accompanied by a corresponding
        update to the description document (java_api_description.md). This
        policy is enforced for AI coding agents via a Jetski rule.
*   Android Sample App (located in ../../android/)
    *   "Motion Photo Playground" - A sample Android application demonstrating
        how to build, check, and extract motion photos using the Java API.
