#include "GameDataPathResolver.h"

#include <spdlog/spdlog.h>

namespace geck::util {

namespace {

    bool pathExists(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

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
