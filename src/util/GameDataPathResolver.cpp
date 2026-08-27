#include "GameDataPathResolver.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace geck::util {

namespace {

    bool isDirectory(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    }

    bool isRegularFile(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    }

    // The archives the engine opens by name from the folder it runs in (gameDbInit). Their presence
    // is what separates an INSTALL from the loose data it ships: a data tree and a mod overlay both
    // hold a `data` folder of their own (ai.txt, city.txt, ...), so that folder alone proves nothing.
    bool hasEngineArchives(const std::filesystem::path& directory) {
        return isRegularFile(directory / "master.dat")
            || isRegularFile(directory / "critter.dat")
            || isRegularFile(directory / "patch000.dat");
    }

} // namespace

bool hasFallout2DataLayout(const std::filesystem::path& path) {
    if (!isDirectory(path)) {
        return false;
    }

    return isDirectory(path / "data") || hasEngineArchives(path);
}

std::filesystem::path looseDataDirectory(const std::filesystem::path& gameRoot) {
    const std::filesystem::path data = gameRoot / "data";
    if (!hasEngineArchives(gameRoot) || !isDirectory(data)) {
        return gameRoot; // a data tree or mod folder IS the loose data; a DAT-only install has none
    }
    return data;
}

std::optional<std::filesystem::path> resolveLooseDataDirectory(const std::filesystem::path& path) {
    const auto gameRoot = resolveGameDataRoot(path);
    if (!gameRoot || gameRoot->empty()) {
        return std::nullopt;
    }
    return looseDataDirectory(*gameRoot);
}

// Append a directory's bundled master.dat/critter.dat (present and not already listed) to `out`.
// Extracted so expandDataPaths stays within the statement-nesting limit.
static void appendBundledDats(const std::filesystem::path& dir, std::vector<std::filesystem::path>& out) {
    for (const char* dat : { "master.dat", "critter.dat" }) {
        const std::filesystem::path datPath = dir / dat;
        if (isRegularFile(datPath) && std::find(out.begin(), out.end(), datPath) == out.end()) {
            out.push_back(datPath);
        }
    }
}

std::vector<std::filesystem::path> expandDataPaths(const std::vector<std::filesystem::path>& dataPaths) {
    std::vector<std::filesystem::path> out;
    const auto already = [&out](const std::filesystem::path& p) {
        return std::find(out.begin(), out.end(), p) != out.end();
    };
    for (const std::filesystem::path& path : dataPaths) {
        // Mount a directory's bundled archives BEFORE the directory itself so the directory's own loose
        // files take precedence over master.dat/critter.dat. Mounts are resolved last-wins, and this
        // matches the engine (fallout2-ce xfileOpen searches the loose data directory before the DATs) —
        // so an edited loose file (e.g. a saved .gam) overrides the packaged copy instead of being
        // shadowed by it. A non-directory entry (a .dat listed directly) just stands alone.
        if (isDirectory(path)) {
            appendBundledDats(path, out);
        }
        if (!already(path)) {
            out.push_back(path); // the directory last, so its loose files win
        }
    }
    return out;
}

std::optional<std::filesystem::path> resolveGameDataRoot(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::nullopt;
    }

    if (path.extension() == ".dat") {
        return path;
    }

#ifdef __APPLE__
    // Determine whether the input is a macOS bundle path.
    // Strict rule: the path must end in ".app" OR be a
    // "Contents/Resources" directory inside an ".app" bundle.
    std::filesystem::path bundleRoot;
    if (path.extension() == ".app") {
        bundleRoot = path;
    } else if (path.filename() == "Resources"
        && path.parent_path().filename() == "Contents"
        && path.parent_path().parent_path().extension() == ".app") {
        bundleRoot = path.parent_path().parent_path();
    }

    if (!bundleRoot.empty()) {
        const std::filesystem::path gogWrappedGameRoot = bundleRoot / "Contents/Resources/game/Fallout 2.app/Contents/Resources/drive_c/Program Files/GOG.com/Fallout 2";
        if (hasFallout2DataLayout(gogWrappedGameRoot)) {
            return gogWrappedGameRoot;
        }
        return std::nullopt;
    }
#endif

    // Before the layout test, not after: a data tree keeps a `data` folder of its own (ai.txt,
    // city.txt, ...), so an install's own "data" would otherwise satisfy hasFallout2DataLayout and
    // come back as the root — leaving looseDataDirectory() to append onto it and reach data/data.
    // The parent must be an install, not merely a folder with a `data` child (which every parent of
    // a path named "data" trivially is).
    if (path.filename() == "data" && hasEngineArchives(path.parent_path())) {
        return path.parent_path();
    }

    if (hasFallout2DataLayout(path)) {
        return path;
    }

    return std::nullopt;
}

bool pathsEquivalent(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code ec;
    if (std::filesystem::equivalent(left, right, ec)) {
        return true;
    }

    auto resolveOrIdentity = [](const std::filesystem::path& p) -> std::filesystem::path {
        if (p.extension() == ".dat") {
            return p;
        }
        if (auto resolved = resolveGameDataRoot(p)) {
            return *resolved;
        }
        return p;
    };

    return resolveOrIdentity(left).lexically_normal() == resolveOrIdentity(right).lexically_normal();
}

void ensureFallbackDataPath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& fallbackDir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(fallbackDir, ec)) {
        return;
    }

    // Plain path equivalence on purpose: pathsEquivalent() resolves entries to a game-data root,
    // which could alias two different directories; the fallback only needs literal dedup.
    const auto alreadyListed = std::any_of(paths.begin(), paths.end(),
        [&fallbackDir](const std::filesystem::path& existing) {
            std::error_code eqEc;
            return std::filesystem::equivalent(existing, fallbackDir, eqEc)
                || existing.lexically_normal() == fallbackDir.lexically_normal();
        });

    if (!alreadyListed) {
        // The stored order is lowest-priority-first (the VFS resolves last-mounted-wins, and the
        // Data Paths table displays the list reversed) — so lowest priority means the FRONT.
        paths.insert(paths.begin(), fallbackDir);
    }
}

} // namespace geck::util
