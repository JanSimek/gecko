#include "ScriptSourceLocator.h"

#include "DataFileSystem.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>
#include <vector>

namespace geck::resource {

namespace {

    std::string toLower(std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // Every regular file under `root` whose lowercased filename equals `wantedFilename`. Uses the
    // error_code iterator overloads so an unreadable subtree is skipped rather than throwing.
    std::vector<std::filesystem::path> filesNamed(const std::filesystem::path& root,
        const std::string& wantedFilename) {
        std::vector<std::filesystem::path> matches;
        std::error_code ec;
        auto it = std::filesystem::recursive_directory_iterator(root,
            std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        while (!ec && it != end) {
            if (it->is_regular_file(ec) && toLower(it->path().filename().string()) == wantedFilename) {
                matches.push_back(it->path());
            }
            it.increment(ec);
        }
        return matches;
    }

    std::optional<ScriptFileLocation> locateFirstExisting(const DataFileSystem& files,
        std::initializer_list<std::filesystem::path> vfsCandidates) {
        for (const std::filesystem::path& vfsPath : vfsCandidates) {
            if (!files.exists(vfsPath)) {
                continue;
            }
            ScriptFileLocation location;
            location.vfsPath = vfsPath;
            if (const auto info = files.sourceInfo(vfsPath);
                info && info->kind == MountedSourceInfo::Kind::Directory) {
                // sourceInfo() reports the winning mount's root; the file itself sits at
                // root/<vfs path>.
                location.diskPath = info->sourcePath / vfsPath;
            } else {
                // A DAT mount, or an unresolvable source: treat as read-only either way.
                location.insideDat = true;
            }
            return location;
        }
        return std::nullopt;
    }

} // namespace

std::string scriptBaseName(const std::string& lstEntry) {
    std::string name = lstEntry;

    // scripts.lst comments should already be split off by the Lst reader; tolerate a raw line anyway.
    if (const std::size_t semicolon = name.find(';'); semicolon != std::string::npos) {
        name.erase(semicolon);
    }

    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    name.erase(name.begin(), std::ranges::find_if_not(name, isSpace));
    name.erase(std::find_if_not(name.rbegin(), name.rend(), isSpace).base(), name.end());

    if (const std::size_t dot = name.rfind('.'); dot != std::string::npos) {
        name.erase(dot);
    }

    std::ranges::transform(name, name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

std::optional<ScriptFileLocation> locateScriptSource(const DataFileSystem& files, const std::string& baseName) {
    if (baseName.empty()) {
        return std::nullopt;
    }
    return locateFirstExisting(files,
        { "scripts/" + baseName + ".ssl", "scripts/source/" + baseName + ".ssl" });
}

std::optional<ScriptFileLocation> locateCompiledScript(const DataFileSystem& files, const std::string& baseName) {
    if (baseName.empty()) {
        return std::nullopt;
    }
    return locateFirstExisting(files, { "scripts/" + baseName + ".int" });
}

std::optional<std::filesystem::path> compiledScriptTarget(const DataFileSystem& files,
    const std::optional<std::filesystem::path>& writableRoot, const std::string& baseName) {
    if (baseName.empty()) {
        return std::nullopt;
    }
    if (const auto compiled = locateCompiledScript(files, baseName);
        compiled && !compiled->insideDat && !compiled->diskPath.empty()) {
        return compiled->diskPath;
    }
    if (writableRoot) {
        return *writableRoot / "scripts" / (baseName + ".int");
    }
    return std::nullopt;
}

std::optional<ScriptSourceInRoot> findScriptSourceInRoots(const std::vector<std::filesystem::path>& sourceRoots,
    const std::string& baseName, bool* ambiguous) {
    if (ambiguous != nullptr) {
        *ambiguous = false;
    }
    if (baseName.empty()) {
        return std::nullopt;
    }

    const std::string wanted = toLower(baseName) + ".ssl";

    for (const std::filesystem::path& root : sourceRoots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            continue;
        }

        std::vector<std::filesystem::path> matches = filesNamed(root, wanted);
        if (matches.empty()) {
            continue;
        }
        // Pick the lexicographically smallest so the result is stable across filesystems
        // (recursive_directory_iterator order is unspecified). The RP has exactly one colliding
        // stem (waypnt: vault13 vs ncr), so this only bites there.
        std::ranges::sort(matches);
        if (ambiguous != nullptr && matches.size() > 1) {
            *ambiguous = true;
        }
        return ScriptSourceInRoot{ root, matches.front() };
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> decompiledSourceTarget(const DataFileSystem& files,
    const std::optional<std::filesystem::path>& writableRoot, const std::string& baseName) {
    if (baseName.empty()) {
        return std::nullopt;
    }
    if (const auto compiled = locateCompiledScript(files, baseName);
        compiled && !compiled->insideDat && !compiled->diskPath.empty()) {
        return compiled->diskPath.parent_path() / (baseName + ".ssl");
    }
    if (writableRoot) {
        return *writableRoot / "scripts" / (baseName + ".ssl");
    }
    return std::nullopt;
}

} // namespace geck::resource
