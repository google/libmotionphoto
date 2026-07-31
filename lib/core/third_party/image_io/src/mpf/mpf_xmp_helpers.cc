#include "image_io/mpf/mpf_xmp_helpers.h"

#include <cstddef>
#include <string>
#include <vector>

#include "image_io/base/message.h"
#include "image_io/base/message_handler.h"
#include "image_io/base/types.h"
#include "image_io/mpf/mpf_info.h"
#include "image_io/mpf/mpf_info_constants.h"
#include "image_io/xmp/xmp_container_metadata.h"
#include "image_io/xmp/xmp_value.h"

namespace photos_editing_formats {
namespace image_io {

namespace {

/// @param message_handler The optional message handler to report the error to.
/// @param message The message text to report.
void ReportError(MessageHandler* message_handler, const std::string& message) {
  if (message_handler != nullptr) {
    message_handler->ReportMessage(Message::kValueError, message);
  }
}

}  // namespace

MpfInfo BuildMpfInfo(
    const std::vector<XmpContainerItemMetadata>& container_items,
    MessageHandler* message_handler) {
  MpfInfo mpf_info;
  if (container_items.empty()) {
    ReportError(message_handler, "Container can't be empty");
    return mpf_info;
  }
  std::vector<UInt32> item_lengths, item_offsets;
  size_t item_count = container_items.size();
  size_t last_item_index = item_count - 1;
  size_t current_offset = 0;
  for (size_t item_index = 0; item_index < item_count; ++item_index) {
    const auto& item = container_items[item_index];
    if (!item.mime.IsValid() || !item.semantic.IsValid()) {
      ReportError(message_handler, "Item must have mime and semantic defined");
      return mpf_info;
    }
    if (item_index == 0 &&
        (!EqualsIgnoreCase(item.semantic, kXmpContainerItemSemanticPrimary) ||
         !EqualsIgnoreCase(item.mime, kXmpContainerItemMimeImageJpeg))) {
      ReportError(
          message_handler,
          "Primary item must be a jpeg image and be first in container");
      return mpf_info;
    }
    if (item_index > 0 && !item.length.IsValid()) {
      ReportError(message_handler, "Non primary item must have length defined");
      return mpf_info;
    }
    if (EqualsIgnoreCase(item.semantic, kXmpContainerItemSemanticMotionPhoto)) {
      if (item_index != last_item_index) {
        ReportError(message_handler,
                    "Motion item must be last in the container");
        return mpf_info;
      }
    } else if (!EqualsIgnoreCase(item.mime, kXmpContainerItemMimeImageJpeg)) {
      ReportError(message_handler,
                  "Non MotionPhoto items must be a jpeg image");
      return mpf_info;
    }
    if (item_index == 0) {
      item_lengths.push_back(0);
      item_offsets.push_back(0);
      current_offset = item.padding.IsValid() ? item.padding.GetValue() : 0;
    } else {
      UInt32 length = item.length.GetValue();
      UInt32 padding = item.padding.IsValid() ? item.padding.GetValue() : 0;
      item_lengths.push_back(length);
      item_offsets.push_back(current_offset);
      current_offset += (length + padding);
    }
  }
  mpf_info.entries.push_back({.attribute = kMpfEntryAttributeTypePrimary});
  for (size_t item_index = 1; item_index < item_count; ++item_index) {
    const auto& item = container_items[item_index];
    if (!EqualsIgnoreCase(item.mime.GetValue(), kXmpContainerItemMimeImageJpeg))
      continue;
    mpf_info.entries.push_back({.attribute = kMpfEntryAttributeFormatJpeg,
                                .image_size = item_lengths[item_index],
                                .image_offset = item_offsets[item_index]});
  }
  if (mpf_info.entries.size() < 2) {
    ReportError(message_handler, "Container must have at least 2 jpeg images");
    return mpf_info;
  }
  mpf_info.is_valid = true;
  return mpf_info;
}

void UpdateMpfInfoEntryImageOffsets(MpfInfo& mpf_info,
                                    size_t primary_image_size,
                                    size_t mpf_segment_offset) {
  size_t offset = primary_image_size - mpf_segment_offset - kMpfEndianOffset;
  for (size_t index = 1; index < mpf_info.entries.size(); ++index) {
    mpf_info.entries[index].image_offset += offset;
  }
}

}  // namespace image_io
}  // namespace photos_editing_formats
