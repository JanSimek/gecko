#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace geck::resource {

class DataFileSystem;

/// A script file resolved through the VFS: where its winning copy lives and whether it can be
/// edited/overwritten in place.
struct ScriptFileLocation {
    std::filesystem::path vfsPath;  // VFS path with generic separators, e.g. "scripts/artemple.ssl"
    std::filesystem::path diskPath; // native path when the winning mount is a directory, else empty
    bool insideDat = false;         // the winning mount is a DAT archive (read-only)
};

/// The bare program name a scripts.lst entry denotes: "ARTEMPLE.INT ; comment" -> "artemple".
/// Lowercased, matching the path convention of the shipped DATs (and the derived
/// scripts/<name>.int / scripts/<name>.ssl probe paths). Empty when the entry is blank.
std::string scriptBaseName(const std::string& lstEntry);

/// The .ssl source for `baseName`, probing scripts/<base>.ssl then scripts/source/<base>.ssl
/// (both layouts appear in modder toolchains). nullopt when no mounted source exists.
std::optional<ScriptFileLocation> locateScriptSource(const DataFileSystem& files, const std::string& baseName);

/// The compiled scripts/<base>.int — the file the engine actually loads.
std::optional<ScriptFileLocation> locateCompiledScript(const DataFileSystem& files, const std::string& baseName);

/// Where a compile should write scripts/<base>.int: over the winning loose copy when there is
/// one (so the engine and VFS keep resolving the same file), otherwise under `writableRoot`.
/// nullopt when the script only exists in DATs (or nowhere) and no writable root is configured.
std::optional<std::filesystem::path> compiledScriptTarget(const DataFileSystem& files,
    const std::optional<std::filesystem::path>& writableRoot, const std::string& baseName);

/// Where a decompile should write scripts/<base>.ssl: next to a loose compiled script when there
/// is one, otherwise under `writableRoot`. The caller is expected to have checked
/// locateScriptSource() first — decompilation must never clobber real source.
std::optional<std::filesystem::path> decompiledSourceTarget(const DataFileSystem& files,
    const std::optional<std::filesystem::path>& writableRoot, const std::string& baseName);

} // namespace geck::resource
