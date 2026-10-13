#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

/// SSL (Fallout 2 script source) front end. sslc (its lexer, parser and the mcpp preprocessor it
/// runs first) defines the language the way fallout2-ce defines the engine: behaviour here follows
/// it, but none of its code is copied - sslc ships no licence.
namespace geck::ssl {

using FileId = uint32_t;

/// Where a token was written. `line` and `column` are 1-based; 0 means unknown.
struct SourceLocation {
    FileId file = 0;
    uint32_t line = 0;
    uint32_t column = 0;
};

struct Diagnostic {
    enum class Severity {
        Warning,
        Error,
    };
    Severity severity = Severity::Error;
    SourceLocation location;
    std::string message;
};

/// Reads source files for the preprocessor. Paths are absolute or relative to the provider's own
/// root, with '/' separators.
class SourceProvider {
public:
    virtual ~SourceProvider() = default;
    virtual std::optional<std::string> readText(const std::filesystem::path& path) const = 0;
};

/// The native filesystem. When a path's exact spelling is absent, each component is matched
/// case-insensitively, as sources written for Windows expect ("..\HEADERS\Define.h").
class DiskSourceProvider final : public SourceProvider {
public:
    std::optional<std::string> readText(const std::filesystem::path& path) const override;
};

/// Files held in memory, keyed by lexically normal path; for tests and editor buffers.
class MemorySourceProvider final : public SourceProvider {
public:
    void add(const std::filesystem::path& path, std::string text);
    std::optional<std::string> readText(const std::filesystem::path& path) const override;

private:
    std::map<std::string, std::string, std::less<>> _files;
};

/// Owns the text of every file a preprocessing run opened, so tokens can view it. Each path is read
/// once; FileId 0 is reserved for "no file".
class SourceManager {
public:
    explicit SourceManager(const SourceProvider& provider);

    /// The file at `path`, read on first use; nullopt when the provider has no such file.
    std::optional<FileId> load(const std::filesystem::path& path);

    const std::filesystem::path& path(FileId file) const;
    std::string_view text(FileId file) const;

private:
    struct File {
        std::filesystem::path path;
        std::string text;
    };

    const SourceProvider& _provider;
    std::deque<File> _files; // deque: element addresses, and so the text tokens view, stay put
    std::map<std::string, FileId, std::less<>> _byPath;
};

} // namespace geck::ssl
