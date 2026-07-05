#include "DataFileSystem.h"

#include "util/GameDataPathResolver.h"
#include "reader/ReaderExceptions.h"
#include "resource/PathUtils.h"
#include "vfs/Dat2FileSystem.hpp"
#include "vfs/VfsppNativeFileSystem.h"

#include <algorithm>
#include <iterator>
#include <regex>
#include <spdlog/spdlog.h>

namespace geck::resource {

DataFileSystem::DataFileSystem()
    : _vfs(std::make_shared<vfspp::VirtualFileSystem>()) {
}

void DataFileSystem::clear() {
    const std::scoped_lock lock(_mutex);
    _vfs = std::make_shared<vfspp::VirtualFileSystem>();
}

void DataFileSystem::addDataPath(const std::filesystem::path& path) {
    const std::scoped_lock lock(_mutex);
    addDataPathLocked(path);
}

namespace {

    // Resolves a user-supplied data path to the actual directory or archive to mount.
    // Returns nullopt when the path cannot be used: a macOS .app bundle without a
    // recognized Fallout 2 layout, or an empty/unresolvable path.
    std::optional<std::filesystem::path> resolveMountRoot(const std::filesystem::path& path) {
        if (path.extension() == ".dat") {
            return path;
        }
        if (auto resolved = util::resolveGameDataRoot(path); resolved && !resolved->empty()) {
            return *resolved;
        }
        // resolveGameDataRoot returns nullopt for macOS .app bundles without a valid GOG
        // wrapper. Do not fall through to mounting the raw .app root (NativeFileSystem
        // would traverse Wine dosdevices symlinks).
        if (path.extension() == ".app") {
            spdlog::warn("macOS bundle '{}' does not contain a recognized Fallout 2 data layout", path.string());
            return std::nullopt;
        }
        if (path.empty()) {
            spdlog::warn("Skipping unresolvable data path: {}", path.string());
            return std::nullopt;
        }
        return path;
    }

} // namespace

void DataFileSystem::addDataPathLocked(const std::filesystem::path& path) {
    const auto mountRoot = resolveMountRoot(path);
    if (!mountRoot) {
        return;
    }
    if (*mountRoot != path) {
        spdlog::info("Resolved data path '{}' to '{}'", path.string(), mountRoot->string());
    }

    std::error_code ec;
    vfspp::IFileSystemPtr fileSystem;
    if (std::filesystem::is_directory(*mountRoot, ec)) {
        // Mount only this directory's loose files; its master.dat/critter.dat are explicit data-path
        // entries of their own (see util::expandDataPaths) rather than silently nested-mounted here.
        fileSystem = std::make_shared<vfspp::NativeFileSystem>("/", mountRoot->string());
    } else if (mountRoot->extension() == ".dat") {
        fileSystem = std::shared_ptr<geck::GeckDat2FileSystem>(new geck::GeckDat2FileSystem("/", mountRoot->string()));
    } else {
        spdlog::error("Unsupported data location: {}", mountRoot->string());
        return;
    }

    if (!fileSystem->Initialize() || !fileSystem->IsInitialized()) {
        spdlog::error("Failed to initialize data path: {}", mountRoot->string());
        return;
    }

    _vfs->AddFileSystem("/", fileSystem);
}

std::optional<std::vector<uint8_t>> DataFileSystem::readRawBytes(const std::filesystem::path& path) const {
    const std::scoped_lock lock(_mutex);
    if (!_vfs) {
        return std::nullopt;
    }

    const std::filesystem::path vfsPath = normalizeVfsPath(path);
    try {
        vfspp::IFilePtr file = _vfs->OpenFile(PathUtils::toVfsPath(vfsPath), vfspp::IFile::FileMode::Read);
        if (!file || !file->IsOpened()) {
            return std::nullopt;
        }

        std::vector<uint8_t> data(file->Size());
        const size_t bytesRead = file->Read(data, file->Size());
        if (bytesRead != data.size()) {
            data.resize(bytesRead);
        }

        return data;
    } catch (const FileReaderException& e) {
        // A corrupt/truncated archive entry (e.g. a failed zlib inflate) must not crash the
        // app: surface it as "no data" so callers (thumbnail rendering, map loading) can
        // degrade gracefully instead of letting the throw reach an abort().
        spdlog::warn("DataFileSystem::readRawBytes: failed to read '{}': {}", path.string(), e.what());
        return std::nullopt;
    }
}

bool DataFileSystem::exists(const std::filesystem::path& path) const {
    const std::scoped_lock lock(_mutex);
    if (!_vfs) {
        return false;
    }

    const std::filesystem::path vfsPath = normalizeVfsPath(path);
    vfspp::IFilePtr file = _vfs->OpenFile(PathUtils::toVfsPath(vfsPath), vfspp::IFile::FileMode::Read);
    return file != nullptr;
}

std::vector<std::filesystem::path> DataFileSystem::list(const std::string& pattern) const {
    const std::scoped_lock lock(_mutex);
    if (!_vfs) {
        return {};
    }

    const std::vector<std::string> allFiles = _vfs->ListAllFiles();
    std::vector<std::filesystem::path> files(allFiles.begin(), allFiles.end());

    if (pattern == "*" || pattern.empty()) {
        return files;
    }

    const std::regex regex(globToRegexPattern(pattern));
    std::vector<std::filesystem::path> filtered;
    std::copy_if(files.begin(), files.end(), std::back_inserter(filtered),
        [&regex](const std::filesystem::path& file) {
            return std::regex_match(file.generic_string(), regex);
        });

    return filtered;
}

void DataFileSystem::refresh() {
    const std::scoped_lock lock(_mutex);
    if (!_vfs) {
        return;
    }

    const auto fileSystemsOpt = _vfs->GetFilesystems("/");
    if (!fileSystemsOpt) {
        return;
    }

    // Snapshot the mounts in their current (priority) order. A NativeFileSystem builds its listing once
    // at Initialize() and never rescans, and Shutdown() wipes its base path (so re-Initialize() can't
    // rebuild it) — the only way to pick up files written on disk this session is a fresh instance. So
    // remove every mount and re-add it in the SAME order (priority preserved): directory mounts are
    // recreated and re-scanned; immutable DAT mounts are reused as-is.
    std::vector<vfspp::IFileSystemPtr> mounts(fileSystemsOpt->get().begin(), fileSystemsOpt->get().end());

    for (const auto& fileSystem : mounts) {
        _vfs->RemoveFileSystem("/", fileSystem);
    }
    for (const auto& fileSystem : mounts) {
        const auto native = std::dynamic_pointer_cast<vfspp::NativeFileSystem>(fileSystem);
        if (!native) {
            _vfs->AddFileSystem("/", fileSystem); // DAT/other: immutable, reuse as-is
            continue;
        }
        auto fresh = std::make_shared<vfspp::NativeFileSystem>("/", native->BasePath());
        if (fresh->Initialize()) {
            _vfs->AddFileSystem("/", fresh);
        } else {
            spdlog::warn("DataFileSystem::refresh: failed to re-scan '{}'; keeping the stale mount", native->BasePath());
            _vfs->AddFileSystem("/", fileSystem); // keep the old mount rather than dropping the path
        }
    }
}

namespace {

    // Classify a mounted filesystem into the source description exposed to callers.
    MountedSourceInfo describeMount(const vfspp::IFileSystemPtr& fileSystem) {
        if (auto datFs = std::dynamic_pointer_cast<geck::GeckDat2FileSystem>(fileSystem)) {
            const std::filesystem::path datPath(datFs->getDatPath());
            std::string label = datPath.filename().string();
            if (label.empty()) {
                label = datPath.generic_string();
            }
            return MountedSourceInfo{ MountedSourceInfo::Kind::Dat, datPath, "DAT (" + label + ")" };
        }

        if (auto nativeFs = std::dynamic_pointer_cast<vfspp::NativeFileSystem>(fileSystem)) {
            const std::filesystem::path nativePath(nativeFs->BasePath());
            std::string label = nativePath.filename().string();
            if (label.empty()) {
                label = nativePath.generic_string();
            }
            return MountedSourceInfo{ MountedSourceInfo::Kind::Directory, nativePath, "Native (" + label + ")" };
        }

        return MountedSourceInfo{ MountedSourceInfo::Kind::Directory, std::filesystem::path(fileSystem->BasePath()), "VFS" };
    }

} // namespace

std::optional<MountedSourceInfo> DataFileSystem::sourceInfo(const std::filesystem::path& path) const {
    const std::scoped_lock lock(_mutex);
    if (!_vfs) {
        return std::nullopt;
    }

    const std::string fullVfsPath = normalizeVfsPath(path).generic_string();

    // Every mounted filesystem is registered under the "/" alias and keys its
    // entries by virtual path (alias + forward-slashed name), so the full VFS
    // path is the lookup key for both DAT and native filesystems.
    const auto fileSystemsOpt = _vfs->GetFilesystems("/");
    if (!fileSystemsOpt) {
        return std::nullopt;
    }

    const auto& fileSystems = fileSystemsOpt->get();
    for (auto it = fileSystems.rbegin(); it != fileSystems.rend(); ++it) {
        const auto& fileSystem = *it;
        if (!fileSystem || !fileSystem->IsInitialized()) {
            continue;
        }

        if (!fileSystem->IsFileExists(fullVfsPath)) {
            continue;
        }

        return describeMount(fileSystem);
    }

    return std::nullopt;
}

std::vector<MountedSourceInfo> DataFileSystem::mounts() const {
    const std::scoped_lock lock(_mutex);
    if (!_vfs) {
        return {};
    }

    const auto fileSystemsOpt = _vfs->GetFilesystems("/");
    if (!fileSystemsOpt) {
        return {};
    }

    std::vector<MountedSourceInfo> result;
    for (const auto& fileSystem : fileSystemsOpt->get()) {
        if (!fileSystem || !fileSystem->IsInitialized()) {
            continue;
        }
        result.push_back(describeMount(fileSystem));
    }
    return result;
}

std::filesystem::path DataFileSystem::normalizeVfsPath(const std::filesystem::path& path) {
    std::filesystem::path normalized = path.generic_string();
    while (normalized.has_root_path() && normalized.generic_string().rfind("//", 0) == 0) {
        normalized = normalized.generic_string().substr(1);
    }

    std::string normalizedString = normalized.generic_string();
    if (normalizedString.empty() || normalizedString.front() != '/') {
        normalizedString.insert(normalizedString.begin(), '/');
    }

    return std::filesystem::path(normalizedString);
}

std::string DataFileSystem::globToRegexPattern(const std::string& pattern) {
    std::string regexPattern;
    regexPattern.reserve(pattern.size() * 2);
    regexPattern.push_back('^');

    for (char ch : pattern) {
        switch (ch) {
            case '*':
                regexPattern.append(".*");
                break;
            case '?':
                regexPattern.push_back('.');
                break;
            case '.':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '^':
            case '$':
            case '+':
            case '|':
            case '\\':
                regexPattern.push_back('\\');
                regexPattern.push_back(ch);
                break;
            default:
                regexPattern.push_back(ch);
                break;
        }
    }

    regexPattern.push_back('$');
    return regexPattern;
}

} // namespace geck::resource
