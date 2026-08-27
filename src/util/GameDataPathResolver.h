#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace geck::util {

/// Check whether a directory contains at least one Fallout 2 data marker
/// (data/ subdirectory, master.dat, critter.dat, or patch000.dat).
bool hasFallout2DataLayout(const std::filesystem::path& path);

/// The loose game files of an install: `<gameRoot>/data`, or the root itself when it has no such
/// folder (a DAT-only install).
///
/// The engine reads every file relative to this directory — it is fallout2.cfg's master_patches, the
/// last database gameDbInit() opens and so the one that wins over master.dat/critter.dat. Editor
/// paths are relative to it too ("proto/scenery/scenery.lst"), which is why it, and not the install
/// root, is what gets mounted and written to: mounting the root buries the loose files at
/// "/data/proto/..." where nothing looks, leaving the packaged DAT copy to answer — a Restoration
/// Project map then resolves its PIDs against vanilla's shorter proto lists and fails to load.
///
/// Only an install descends: a folder is one when it holds the archives the engine opens by name
/// (master.dat / critter.dat / patch000.dat). A data tree and a loose mod overlay both contain a
/// `data` folder of their own, so descending on that alone would bury them in their own data/data.
std::filesystem::path looseDataDirectory(const std::filesystem::path& gameRoot);

/// The directory a user-supplied data-path entry actually stands for: its install resolved
/// (resolveGameDataRoot), then descended into that install's loose data (looseDataDirectory).
/// nullopt when the entry names no recognisable game layout at all.
///
/// Mounting and writing must agree on this — a save that lands anywhere but the mounted directory is
/// invisible to the editor that wrote it — so both go through this one function.
std::optional<std::filesystem::path> resolveLooseDataDirectory(const std::filesystem::path& path);

/// Expand each directory that ships master.dat / critter.dat into the directory followed by those DATs,
/// so every mounted archive is an explicit data-path entry rather than a silent nested mount. The DATs
/// come first and the directory last: mounts resolve last-wins, so the folder's loose files override
/// the archives it ships, as they do in the engine. `.dat` entries and DAT-less directories pass
/// through untouched; an already-listed path is never duplicated. Used to migrate older folder-only
/// settings and when adding a folder in the UI.
std::vector<std::filesystem::path> expandDataPaths(const std::vector<std::filesystem::path>& dataPaths);

/// Attempt to resolve a user-supplied path to a Fallout 2 game data root.
///
/// Resolution rules (evaluated in order):
///   1. Empty path -> nullopt
///   2. .dat extension -> returned as-is
///   3. macOS .app bundle or Contents/Resources -> probe GOG wrapper path
///   4. Filename is "data" and the parent is an install -> return parent
///   5. Has Fallout 2 data layout -> return path
///   6. Otherwise -> nullopt
///
/// Rule 4 precedes rule 5 deliberately: an install's `data` contains a `data` folder of its own, so
/// it would otherwise pass rule 5 and be reported as the root. It also tests the parent for the
/// engine's archives — every parent of a path named "data" has a `data` child by definition, so the
/// layout test alone would pull a standalone data tree up to its unrelated parent.
std::optional<std::filesystem::path> resolveGameDataRoot(const std::filesystem::path& path);

/// Compare two paths for equivalence.  Uses std::filesystem::equivalent first,
/// falling back to comparing resolved+lexically_normal paths.
bool pathsEquivalent(const std::filesystem::path& left, const std::filesystem::path& right);

/// Add `fallbackDir` to `paths` unless an equivalent entry is already present or the directory
/// does not exist. It is inserted at the FRONT — the stored order is lowest-priority-first (the
/// VFS resolves last-mounted-wins) — so the fallback only fills gaps the configured paths leave.
/// Used to keep the editor's own bundled resources (blank tile, overlay art, ...) mounted no
/// matter how the user reconfigures the data paths — editor-essential assets are not game data,
/// so they must not disappear with a settings change.
void ensureFallbackDataPath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& fallbackDir);

} // namespace geck::util
