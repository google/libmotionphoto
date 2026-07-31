#ifndef IMAGE_IO_XMP_XMP_READER_H_  // NOLINT
#define IMAGE_IO_XMP_XMP_READER_H_  // NOLINT

#include "image_io/base/message_handler.h"
#include "image_io/xml/xml_reader.h"
#include "image_io/xmp/xmp_dispatch_handler.h"
#include "image_io/xmp/xmp_element_handler.h"

namespace photos_editing_formats {
namespace image_io {

/// A fairly simple wrapper arounds an XmlReader that makes it easier to read
/// XMP and call the appropriate functions in one or more XmpElementHandlers.
class XmpReader {
 public:
  explicit XmpReader(MessageHandler* message_handler);
  virtual ~XmpReader() = default;

  /// Allows client code to add an element handler to the reader.
  /// @param element_handler An element handler to deal with the various parts
  /// of elements as they are parsed.
  void AddElementHandler(XmpElementHandler* element_handler) {
    handler_.AddElementHandler(element_handler);
  }

  /// Allows client code to remove a previously added element handler.
  /// @param element_handler The handler to remove.
  void RemoveElementHandler(XmpElementHandler* element_handler) {
    handler_.RemoveElementHandler(element_handler);
  }

  /// A externally initialized data line map can be used for error messages
  /// instead of the internally built map. Otherwise the internal map is used.
  /// @param data_line_map The externally initialized data line map to use.
  void SetDataLineMap(const DataLineMap* data_line_map) {
    reader_.SetDataLineMap(data_line_map);
  }

  /// Allows the XmpReader to ignore a missing final <?xpacket...?> value.
  /// This condition is normally an error, but if this function is called, it
  /// can be ignored or checked for by calling IsMissingFinalXpacket().
  void SetIgnoreMissingFinalXpacket();

  /// @return Whether the final packet was missing.
  bool IsMissingFinalXpacket() const;

  /// Starts the parsing process.
  /// @return Whether the start process was successful.
  bool StartParse();

  /// Parses the XMP/XML syntax contained in the string value. This function
  /// may be called multiple times if the entire XMP/XML syntax is not contained
  /// in a single string.
  /// @return Whether the syntax was parsed successfully.
  bool Parse(const std::string& value) { return reader_.Parse(value); }

  /// Parses the XMP/XML syntax contained in a the data segment in the given
  /// range.
  /// @param start_location The location in the range at which to start parsing.
  /// @param range The range in the data segment that contains the XMP/XML
  /// syntax that is to be parsed.
  /// @param segment The data segment containing the XMP/XML syntax.
  /// @return Whether the syntax was parsed successfully.
  bool Parse(size_t start_location, const DataRange& range,
             const DataSegment& segment) {
    return reader_.Parse(start_location, range, segment);
  }

  /// @return The number of bytes that have been parsed so far.
  size_t GetBytesParsed() const { return reader_.GetBytesParsed(); }

  /// Finishes the parsing process.
  /// @return Whether the parsing could be finished without error.
  virtual bool FinishParse();

  /// @return Whether the parsing process produced errors.
  virtual bool HasErrors() const;

 private:
  XmlReader reader_;
  XmpDispatchHandler handler_;
  std::shared_ptr<bool> missing_final_xpacket_flag_;
};

}  // namespace image_io
}  // namespace photos_editing_formats

#endif  // IMAGE_IO_XMP_XMP_READER_H_  // NOLINT
