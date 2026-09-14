#include "cli/MapLoad.h"

#include "format/map/Map.h"
#include "format/pro/Pro.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>
#include <zlib.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
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

    bool isGzip(const std::vector<uint8_t>& bytes) {
        return bytes.size() >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B;
    }

    // Save slots hold every map gzip-compressed: the engine writes them with fileCopyCompressed and
    // loads them back through _gzdecompress_file (fallout2-ce loadsave.cc _GameMap2Slot /
    // _SlotMap2Game), while the live copies under data/maps are plain. windowBits 15 + 16 accepts
    // exactly the gzip container, so a truncated or foreign stream fails instead of half-inflating.
    std::vector<uint8_t> gunzip(const std::vector<uint8_t>& compressed) {
        z_stream zs{};
        if (inflateInit2(&zs, 15 + 16) != Z_OK) {
            throw std::runtime_error("zlib inflateInit2 failed");
        }
        zs.next_in = const_cast<Bytef*>(compressed.data());
        zs.avail_in = static_cast<uInt>(compressed.size());

        std::vector<uint8_t> out;
        std::vector<uint8_t> chunk(1 << 16);
        int rc = Z_OK;
        while (rc != Z_STREAM_END) {
            zs.next_out = chunk.data();
            zs.avail_out = static_cast<uInt>(chunk.size());
            rc = inflate(&zs, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) {
                inflateEnd(&zs);
                throw std::runtime_error("gzip inflate failed (zlib result " + std::to_string(rc) + ")");
            }
            out.insert(out.end(), chunk.data(), chunk.data() + (chunk.size() - zs.avail_out));
            if (rc != Z_STREAM_END && zs.avail_in == 0 && zs.avail_out != 0) {
                inflateEnd(&zs);
                throw std::runtime_error("gzip stream is truncated");
            }
        }
        inflateEnd(&zs);
        return out;
    }
} // namespace

std::unique_ptr<Map> loadMap(resource::GameResources& resources, const std::string& mapPath,
    std::string* error) {
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
        if (error != nullptr) {
            *error = "not found in the mounted data or on disk";
        }
        return nullptr;
    }
    try {
        if (isGzip(*bytes)) {
            bytes = gunzip(*bytes);
        }
        MapReader reader(makeProtoLoader(resources));
        return reader.openFile(mapPath, *bytes);
    } catch (const std::exception& e) {
        spdlog::debug("loadMap: parse failed for {}: {}", mapPath, e.what());
        if (error != nullptr) {
            *error = e.what();
        }
        return nullptr;
    }
}

} // namespace geck::cli
