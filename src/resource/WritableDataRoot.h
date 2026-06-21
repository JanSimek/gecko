#pragma once

#include <filesystem>
#include <string>

namespace geck::resource {

class DataFileSystem;

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
