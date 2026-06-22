#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace geck::resource {

class DataFileSystem;

/// Outcome of a map-name edit. `error` is a human-readable explanation when `ok` is false (e.g. the
/// edit would make maps.txt invalid, or the lookup_name key is missing) — the UI surfaces it.
struct MapNameEditResult {
    bool ok = true;
    std::string error;
};

/// Persist edited map names to the writable overlay, with validation. The lookup name (maps.txt
/// `lookup_name`) and/or the per-elevation display name (map.msg) are applied only when provided.
///
/// maps.txt is loaded into the round-trip document, the field is set, and the WHOLE file is validated;
/// if it would be invalid (engine rules) or the section/key is missing, NOTHING is written and `ok` is
/// false. Otherwise both files are copied out of their DAT (if needed) and written. Qt-free so the
/// whole save path is unit-testable without the panel.
MapNameEditResult saveMapNames(DataFileSystem& files, const std::filesystem::path& writableRoot,
    int mapIndex, int elevation,
    const std::optional<std::string>& lookupName, const std::optional<std::string>& displayName);

} // namespace geck::resource
