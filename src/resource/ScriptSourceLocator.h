#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace geck::resource {

class DataFileSystem;

/// A script's SSL source found inside a marked source-root directory (e.g. FRP scripts_src).
struct ScriptSourceInRoot {
    std::filesystem::path sourceRoot; // the marked root it was found under (the editor's workspace folder)
    std::filesystem::path file;       // native path to the <name>.ssl within that root
};

/// Find `<baseName>.ssl` (case-insensitive) anywhere under the given native source roots, searched
/// in order. The RP organizes sources by area (scripts_src/<area>/<name>.ssl) with the compiled
/// .int flat, so the match is by base name regardless of subdirectory — never by the source layout.
/// `.ssl.tmp` preprocessor artefacts are ignored. On a duplicate stem the lexicographically smallest
/// path wins (deterministic); pass `ambiguous` to learn a collision happened. nullopt when unmatched.
std::optional<ScriptSourceInRoot> findScriptSourceInRoots(const std::vector<std::filesystem::path>& sourceRoots,
    const std::string& baseName, bool* ambiguous = nullptr);

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

} // namespace geck::resource
