#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace geck::resource {

class DataFileSystem;

/// The data path edited copies should be written to: the last entry in `dataPaths` that is an existing
/// directory (DAT/archive paths are read-only and skipped), or nullopt if there is none. Editing writes
/// into a folder the user can see and manage in their Data Paths — there is no hidden auto-mounted
/// location. Being last means its copies shadow the archives mounted before it.
std::optional<std::filesystem::path> findWritableDataPath(const std::vector<std::filesystem::path>& dataPaths);

/// Same, but honouring an explicit user choice first: when `preferred` is non-empty, is one of
/// `dataPaths`, and is an existing directory, it is the writable root regardless of list order.
/// Otherwise (unset, removed from the list, missing on disk, or an archive) this falls back to the
/// positional rule above — with a warning log, so a configured-but-unusable choice is never silent.
std::optional<std::filesystem::path> findWritableDataPath(const std::vector<std::filesystem::path>& dataPaths,
    const std::filesystem::path& preferred);

/// True when two data-path entries denote the same location: std::filesystem::equivalent when both
/// exist (symlinks, case variance), lexical comparison otherwise. Deliberately NOT
/// util::pathsEquivalent — its resolve-to-game-data-root step would conflate a folder with its
/// data/ subfolder. Shared by the marker-membership checks here and in Settings so they can't drift.
bool sameDataPathEntry(const std::filesystem::path& a, const std::filesystem::path& b);

/// Ensure a loose, writable copy of a VFS file exists under `writableRoot`, and return its native path.
///
/// Editing game data that lives inside a read-only DAT requires copying it out first. If a copy is
/// already present at `writableRoot / vfsRelPath` (from a prior edit), it is returned untouched — the
/// edited copy is the source of truth and must not be clobbered. Otherwise the file's current bytes
/// (resolved through the VFS — from a DAT or any mounted directory) are written there verbatim (binary,
/// so CRLF survives), creating the nested directory structure.
///
/// The writable root is expected to be mounted LAST in the VFS so this copy shadows the original on the
/// next read. Throws std::runtime_error if the source file can't be read or the copy can't be written.
std::filesystem::path ensureWritableCopy(const DataFileSystem& files,
    const std::filesystem::path& writableRoot, const std::string& vfsRelPath);

} // namespace geck::resource
