#include "ssl/Source.h"

#include "reader/TextParsing.h"
#include "util/FileIo.h"

#include <algorithm>
#include <system_error>

namespace geck::ssl {

namespace {

    using text::equalsIgnoreCase;

    // `path` with each component replaced by the directory entry that matches it ignoring case.
    std::optional<std::filesystem::path> resolveIgnoringCase(const std::filesystem::path& path) {
        std::filesystem::path resolved = path.root_path();
        std::error_code ec;
        for (const auto& part : path.relative_path()) {
            if (std::filesystem::exists(resolved / part, ec)) {
                resolved /= part;
                continue;
            }
            const std::filesystem::path dir = resolved.empty() ? std::filesystem::path(".") : resolved;
            std::optional<std::filesystem::path> match;
            for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                if (equalsIgnoreCase(it->path().filename().string(), part.string())) {
                    match = it->path().filename();
                    break;
                }
            }
            if (!match.has_value()) {
                return std::nullopt;
            }
            resolved /= *match;
        }
        return resolved;
    }

    std::string key(const std::filesystem::path& path) {
        return path.lexically_normal().generic_string();
    }

} // namespace

std::optional<std::string> DiskSourceProvider::readText(const std::filesystem::path& path) const {
    if (auto text = io::tryReadFile(path); text.has_value()) {
        return text;
    }
    if (const auto resolved = resolveIgnoringCase(path.lexically_normal()); resolved.has_value()) {
        return io::tryReadFile(*resolved);
    }
    return std::nullopt;
}

void MemorySourceProvider::add(const std::filesystem::path& path, std::string text) {
    _files[key(path)] = std::move(text);
}

std::optional<std::string> MemorySourceProvider::readText(const std::filesystem::path& path) const {
    if (const auto it = _files.find(key(path)); it != _files.end()) {
        return it->second;
    }
    return std::nullopt;
}

SourceManager::SourceManager(const SourceProvider& provider)
    : _provider(provider) {
    _files.emplace_back(); // FileId 0: no file
}

std::optional<FileId> SourceManager::load(const std::filesystem::path& path) {
    const std::string normal = key(path);
    if (const auto it = _byPath.find(normal); it != _byPath.end()) {
        return it->second;
    }
    auto text = _provider.readText(normal);
    if (!text.has_value()) {
        return std::nullopt;
    }
    const auto id = static_cast<FileId>(_files.size());
    _files.emplace_back(std::filesystem::path(normal), std::move(*text));
    _byPath.try_emplace(normal, id);
    return id;
}

const std::filesystem::path& SourceManager::path(FileId file) const {
    return _files.at(file).path;
}

std::string_view SourceManager::text(FileId file) const {
    return _files.at(file).text;
}

std::string_view SourceManager::line(FileId file, uint32_t line) const {
    std::string_view text = this->text(file);
    for (uint32_t current = 1; current < line; ++current) {
        const auto next = text.find('\n');
        if (next == std::string_view::npos) {
            return {};
        }
        text.remove_prefix(next + 1);
    }
    text = text.substr(0, text.find('\n'));
    if (!text.empty() && text.back() == '\r') {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace geck::ssl
