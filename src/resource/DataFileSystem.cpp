#include "DataFileSystem.h"

#include "../util/GameDataPathResolver.h"
#include "../util/PathUtils.h"
#include "../util/ResourcePaths.h"
#include "../vfs/Dat2FileSystem.hpp"
#include "../vfs/VfsppNativeFileSystem.h"

#include <regex>
#include <spdlog/spdlog.h>

namespace geck::resource {

DataFileSystem::DataFileSystem()
    : _vfs(std::make_shared<vfspp::VirtualFileSystem>()) {
}

void DataFileSystem::clear() {
    _vfs = std::make_shared<vfspp::VirtualFileSystem>();
}

void DataFileSystem::addDataPath(const std::filesystem::path& path) {
    std::filesystem::path mountRoot;
    if (path.extension() == ".dat") {
        mountRoot = path;
    } else if (auto resolved = util::resolveGameDataRoot(path)) {
        mountRoot = *resolved;
    } else {
        // resolveGameDataRoot returns nullopt for macOS .app bundles without a
        // valid GOG wrapper.  Do not fall through to mounting the raw .app root
        // (NativeFileSystem would traverse Wine dosdevices symlinks).
        if (path.extension() == ".app") {
            spdlog::warn("macOS bundle '{}' does not contain a recognized Fallout 2 data layout", path.string());
            return;
        }
        mountRoot = path;
    }

    if (mountRoot.empty()) {
        spdlog::warn("Skipping unresolvable data path: {}", path.string());
        return;
    }
    if (mountRoot != path) {
        spdlog::info("Resolved data path '{}' to '{}'", path.string(), mountRoot.string());
    }

    std::error_code ec;
    vfspp::IFileSystemPtr fileSystem;

    if (std::filesystem::is_directory(mountRoot, ec)) {
        fileSystem = std::make_shared<vfspp::NativeFileSystem>(mountRoot.string());

        const std::filesystem::path masterDat = mountRoot / ResourcePaths::Dat::MASTER;
        if (std::filesystem::exists(masterDat, ec) && std::filesystem::is_regular_file(masterDat, ec)) {
            addDataPath(masterDat);
        }

        const std::filesystem::path critterDat = mountRoot / ResourcePaths::Dat::CRITTER;
        if (std::filesystem::exists(critterDat, ec) && std::filesystem::is_regular_file(critterDat, ec)) {
            addDataPath(critterDat);
        }
    } else if (mountRoot.extension() == ".dat") {
        fileSystem = std::shared_ptr<geck::GeckDat2FileSystem>(new geck::GeckDat2FileSystem(mountRoot.string()));
    } else {
        spdlog::error("Unsupported data location: {}", mountRoot.string());
        return;
    }

    fileSystem->Initialize();
    if (!fileSystem->IsInitialized()) {
        spdlog::error("Failed to initialize data path: {}", mountRoot.string());
        return;
    }

    _vfs->AddFileSystem("/", fileSystem);
    spdlog::info("Location '{}' was added to the data path", mountRoot.string());
}

std::optional<std::vector<uint8_t>> DataFileSystem::readRawBytes(const std::filesystem::path& path) const {
    if (!_vfs) {
        return std::nullopt;
    }

    const std::filesystem::path vfsPath = normalizeVfsPath(path);
    vfspp::IFilePtr file = _vfs->OpenFile(PathUtils::createNormalizedFileInfo(vfsPath), vfspp::IFile::FileMode::Read);
    if (!file || !file->IsOpened()) {
        return std::nullopt;
    }

    std::vector<uint8_t> data(file->Size());
    const size_t bytesRead = file->Read(data, file->Size());
    if (bytesRead != data.size()) {
        data.resize(bytesRead);
    }

    return data;
}

bool DataFileSystem::exists(const std::filesystem::path& path) const {
    if (!_vfs) {
        return false;
    }

    const std::filesystem::path vfsPath = normalizeVfsPath(path);
    vfspp::IFilePtr file = _vfs->OpenFile(PathUtils::createNormalizedFileInfo(vfsPath), vfspp::IFile::FileMode::Read);
    return file != nullptr;
}

std::vector<std::filesystem::path> DataFileSystem::list(const std::string& pattern) const {
    std::vector<std::filesystem::path> files;
    if (!_vfs) {
        return files;
    }

    for (const std::string& file : _vfs->ListAllFiles()) {
        files.emplace_back(file);
    }

    if (pattern == "*" || pattern.empty()) {
        return files;
    }

    const std::regex regex(globToRegexPattern(pattern));
    std::vector<std::filesystem::path> filtered;
    for (const auto& file : files) {
        if (std::regex_match(file.generic_string(), regex)) {
            filtered.push_back(file);
        }
    }

    return filtered;
}

std::optional<MountedSourceInfo> DataFileSystem::sourceInfo(const std::filesystem::path& path) const {
    if (!_vfs) {
        return std::nullopt;
    }

    const std::string fullVfsPath = normalizeVfsPath(path).generic_string();

    std::string relativePath = fullVfsPath;
    if (!relativePath.empty() && relativePath.front() == '/') {
        relativePath.erase(relativePath.begin());
    }

    const auto& fileSystems = _vfs->GetFilesystems("/");
    for (auto it = fileSystems.rbegin(); it != fileSystems.rend(); ++it) {
        const auto& fileSystem = *it;
        if (!fileSystem || !fileSystem->IsInitialized()) {
            continue;
        }

        // Try with BasePath + relative path (works for DAT and relative VFS paths)
        const vfspp::FileInfo fileInfo(fileSystem->BasePath(), relativePath, false);
        bool found = fileSystem->IsFileExists(fileInfo);

        // NativeFileSystem stores absolute paths as keys; ListAllFiles returns
        // these absolute paths directly. When that happens, BasePath + path
        // produces a doubled path. Fall back to using the full path as-is.
        if (!found) {
            const vfspp::FileInfo absoluteFileInfo(fullVfsPath);
            found = fileSystem->IsFileExists(absoluteFileInfo);
        }

        if (!found) {
            continue;
        }

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

    return std::nullopt;
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
