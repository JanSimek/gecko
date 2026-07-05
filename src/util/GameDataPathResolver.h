#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace geck::util {

/// Check whether a directory contains at least one Fallout 2 data marker
/// (data/ subdirectory, master.dat, critter.dat, or patch000.dat).
bool hasFallout2DataLayout(const std::filesystem::path& path);

/// Expand each directory that ships master.dat / critter.dat into the directory followed by those DATs,
/// so every mounted archive is an explicit data-path entry rather than a silent nested mount. Order is
/// preserved — the directory precedes its DATs, matching the legacy mount order so priority is
/// unchanged. `.dat` entries and DAT-less directories pass through untouched; an already-listed path is
/// never duplicated. Used to migrate older folder-only settings and when adding a folder in the UI.
std::vector<std::filesystem::path> expandDataPaths(const std::vector<std::filesystem::path>& dataPaths);

/// Attempt to resolve a user-supplied path to a Fallout 2 game data root.
///
/// Resolution rules (evaluated in order):
///   1. Empty path -> nullopt
///   2. .dat extension -> returned as-is
///   3. macOS .app bundle or Contents/Resources -> probe GOG wrapper path
///   4. Has Fallout 2 data layout -> return path
///   5. Filename is "data" and parent has layout -> return parent
///   6. Otherwise -> nullopt
std::optional<std::filesystem::path> resolveGameDataRoot(const std::filesystem::path& path);

/// Compare two paths for equivalence.  Uses std::filesystem::equivalent first,
/// falling back to comparing resolved+lexically_normal paths.
bool pathsEquivalent(const std::filesystem::path& left, const std::filesystem::path& right);

/// Append `fallbackDir` to `paths` unless an equivalent entry is already present or the directory
/// does not exist. Appending (not prepending) keeps it at the lowest priority: it only fills gaps
/// the configured paths leave. Used to keep the editor's own bundled resources (blank tile,
/// overlay art, ...) mounted no matter how the user reconfigures the data paths — editor-essential
/// assets are not game data, so they must not disappear with a settings change.
void ensureFallbackDataPath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& fallbackDir);

} // namespace geck::util
