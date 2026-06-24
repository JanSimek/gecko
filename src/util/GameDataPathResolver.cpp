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

} // namespace

bool hasFallout2DataLayout(const std::filesystem::path& path) {
    if (!isDirectory(path)) {
        return false;
    }

    return isDirectory(path / "data")
        || isRegularFile(path / "master.dat")
        || isRegularFile(path / "critter.dat")
        || isRegularFile(path / "patch000.dat");
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

    if (hasFallout2DataLayout(path)) {
        return path;
    }

    if (path.filename() == "data") {
        const std::filesystem::path parent = path.parent_path();
        if (hasFallout2DataLayout(parent)) {
            return parent;
        }
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

} // namespace geck::util
