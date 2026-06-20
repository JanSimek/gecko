#include "cli/MapLoad.h"

#include "format/map/Map.h"
#include "format/pro/Pro.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

namespace geck::cli {

std::function<Pro*(std::uint32_t)> makeProtoLoader(resource::GameResources& resources) {
    return [&resources](std::uint32_t pid) -> Pro* {
        try {
            return resources.loadPro(pid);
        } catch (const std::exception& e) {
            spdlog::debug("proLoad: pid {} failed: {}", pid, e.what());
            return nullptr;
        }
    };
}

namespace {
    // Read a file straight off disk as raw bytes; nullopt if it isn't a readable regular file.
    std::optional<std::vector<uint8_t>> readDiskBytes(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            return std::nullopt;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
} // namespace

std::unique_ptr<Map> loadMap(resource::GameResources& resources, const std::string& mapPath) {
    // Prefer the VFS so a map inside a DAT ("/maps/desert1.map") or a mounted data directory
    // resolves the same way the editor sees it.
    auto bytes = resources.files().readRawBytes(mapPath);
    if (!bytes) {
        // Fall back to the real filesystem. The VFS only serves entries discovered when a mount was
        // initialised, so a .map written *after* mount time — one `generate` just produced, or any
        // file outside the mounted data — is invisible to it (this is why the long-running MCP
        // server couldn't render what it had just generated). Reading the path off disk lets
        // render / extract / analyze operate on a freshly generated or arbitrary map without
        // re-mounting. Proto/tile lookups still come from the mounts, which is all the .map needs.
        bytes = readDiskBytes(mapPath);
    }
    if (!bytes) {
        return nullptr;
    }
    try {
        MapReader reader(makeProtoLoader(resources));
        return reader.openFile(mapPath, *bytes);
    } catch (const std::exception& e) {
        spdlog::debug("loadMap: parse failed for {}: {}", mapPath, e.what());
        return nullptr;
    }
}

} // namespace geck::cli
