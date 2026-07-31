#ifndef IMAGE_IO_XMP_XMP_CONTAINER_METADATA_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_CONTAINER_METADATA_H_  // NOLINT

#include <string>
#include <vector>

#include "image_io/base/types.h"
#include "image_io/xmp/xmp_value.h"

namespace photos_editing_formats {
namespace image_io {

/// Constants for Container metadata.
constexpr char kXmpContainerPrefix[] = "Container";
constexpr char kXmpContainerUri[] =
    "http://ns.google.com/photos/1.0/container/";
constexpr char kXmpContainerDirectory[] = "Directory";
constexpr char kXmpContainerVersion[] = "Version";
constexpr char kXmpContainerItem[] = "Item";

/// Constants for ContainerItem metadata.
constexpr char kXmpContainerItemPrefix[] = "Item";
constexpr char kXmpContainerItemUri[] =
    "http://ns.google.com/photos/1.0/container/item/";
constexpr char kXmpContainerItemDataUri[] = "URI";
constexpr char kXmpContainerItemLabel[] = "Label";
constexpr char kXmpContainerItemLength[] = "Length";
constexpr char kXmpContainerItemMime[] = "Mime";
constexpr char kXmpContainerItemPadding[] = "Padding";
constexpr char kXmpContainerItemSemantic[] = "Semantic";

/// Constants for common values.
constexpr char kXmpContainerVersionCurrent[] = "1.0";
constexpr char kXmpContainerItemSemanticPrimary[] = "Primary";
constexpr char kXmpContainerItemSemanticGainMap[] = "GainMap";
constexpr char kXmpContainerItemSemanticMotionPhoto[] = "MotionPhoto";
constexpr char kXmpContainerItemMimeImageJpeg[] = "image/jpeg";
constexpr char kXmpContainerItemMimeImageHeic[] = "image/heic";
constexpr char kXmpContainerItemMimeVideoMp4[] = "video/mp4";

/// The metadata associated with an XMP Container:Item.
struct XmpContainerItemMetadata {
  /// The semantic of the item.
  XmpValue<std::string> semantic;

  /// The mime format of the item.
  XmpValue<std::string> mime;

  /// The data URI of the item.
  XmpValue<std::string> data_uri;

  /// The label of the item.
  XmpValue<std::string> label;

  /// The length value of the item.
  XmpValue<Int64> length;

  /// The padding value of the item.
  XmpValue<Int64> padding;

  bool operator==(const XmpContainerItemMetadata& rhs) const {
    return semantic == rhs.semantic && mime == rhs.mime &&
           data_uri == rhs.data_uri && label == rhs.label &&
           length == rhs.length && padding == rhs.padding;
  }

  bool operator!=(const XmpContainerItemMetadata& rhs) const {
    return !(*this == rhs);
  }
};

/// The metadata associated with an XMP Container
struct XmpContainerMetadata {
  /// The version of the container.
  XmpValue<std::string> version;

  /// The items in the container.
  std::vector<XmpContainerItemMetadata> items;

  bool operator==(const XmpContainerMetadata& rhs) const {
    return version == rhs.version && items == rhs.items;
  }

  bool operator!=(const XmpContainerMetadata& rhs) const {
    return !(*this == rhs);
  }
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_CONTAINER_METADATA_H_  // NOLINT
