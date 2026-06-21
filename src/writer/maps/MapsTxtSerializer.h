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

/// Insert a new `[Map index]` section (zero-padded header) with `lookup_name` + `map_name` lines, in
/// index order. Returns false if a section with that index already exists. The caller should keep the
/// run gapless (see validateMapsTxt) — append at max+1.
bool addSection(MapsTxtDocument& doc, int index, const std::string& lookupName, const std::string& mapName);

/// Remove the `[Map index]` section. Returns false if absent. Does NOT renumber the remaining
/// sections — removing a non-last section leaves a gap that validateMapsTxt reports as an error.
bool removeSection(MapsTxtDocument& doc, int index);

} // namespace geck::writer
