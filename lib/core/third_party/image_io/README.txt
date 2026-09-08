Photos Formats ImageIO Library

This library provides a lightweight interface to the images and metadata
contained in the files that Google Photos reads and writes.


The directory structure and code dependencies for this package are:

*   base - Basic components used by other parts of the library
    *   Code in this subdirectory may not have any dependencies other than the
        C++ library
*   extras - Other basic components that have //third_party (or other) dependencies
    *   Code in this library may depend on base code and have dependencies on
        //third_party/...
*   iso - Objects that can be encoded and decoded following the ISO standards.
    *   Code in this library may depend only on components in the base directory
*   jpeg - The JPEG components
    *   Code in this library may depend only on components in base and extras
*   utils - Useful utility type components
    *   Code in this library may depend only on components in the base directory
*   xml - A simplfied but very memory efficient XML parser
    *   Code in this library may depend only on components in the base directory
*   xmp - Extends the xml components to handle the parsing and processing of XMP data.
    *   Code in this library may depend on components in the base and xml
        directories.

