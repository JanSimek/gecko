#pragma once

#include "format/maps/MapsTxtDocument.h"

#include <optional>
#include <string>

namespace geck::writer {

/// Serialise a @ref MapsTxtDocument back to bytes. Every line is emitted from its raw content joined
/// with `\n` (plus a trailing `\n` iff `finalNewline`), so an unedited document is reproduced exactly
/// (modulo CRLF->LF). The engine writes LF and tolerates both (fallout2-ce config.cc).
std::string serializeMapsTxt(const MapsTxtDocument& doc);

/// The value of `key` (case-insensitive) in `[Map sectionIndex]`, or nullopt if the section or key is
/// absent.
std::optional<std::string> findField(const MapsTxtDocument& doc, int sectionIndex, const std::string& key);

/// Set `key`'s value in `[Map sectionIndex]`, rebuilding only that line and preserving its inline
/// comment. Returns false if the section or key is absent (the caller decides whether to add it).
bool setField(MapsTxtDocument& doc, int sectionIndex, const std::string& key, const std::string& value);

} // namespace geck::writer
