#pragma once

#include <filesystem>
#include <string>

namespace geck::writer {

/// In-place patch of a loose `data/maps.txt`: replace the `lookup_name` value of the `[Map <mapIndex>]`
/// section, preserving every other byte — CRLF line endings, `;` comments, blank lines, and the keys
/// the reader doesn't model (music, ambient_sfx, can_rest_here, random_start_point_N, …). This is a
/// targeted edit of one field, NOT a re-serialization of the parsed model (which would drop all of the
/// above). Section matching mirrors MapsTxtReader (`[Map NNN]`, zero-padded or not).
///
/// @return true if the section's `lookup_name` line was found and rewritten; false if the map section
///         (or its lookup_name) is absent — the caller should report "map not in maps.txt".
bool updateLookupName(const std::filesystem::path& mapsTxtPath, int mapIndex, const std::string& newName);

/// In-place patch of a loose `map.msg`: set a map's per-elevation display name. The message id is
/// `mapIndex*3 + elevation + 200` (the engine / MapNameResolver formula). Keeps the message's audio
/// field and every other line (CRLF, `#` comments) verbatim; if the id isn't present, appends a new
/// `{id}{}{text}` line. Parses the `{id}{audio}{text}` triple by brace matching (not the reader's
/// greedy regex) so a text value containing `}{` round-trips.
///
/// @return true on patch or append; false for an invalid mapIndex (<0) or elevation (not 0..2).
bool updateDisplayName(const std::filesystem::path& mapMsgPath, int mapIndex, int elevation, const std::string& newText);

} // namespace geck::writer
