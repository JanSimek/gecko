#include "DataFileSystem.h"

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
    vfspp::IFileSystemPtr fileSystem;

    if (std::filesystem::is_directory(path)) {
        fileSystem = std::make_shared<vfspp::NativeFileSystem>(path.string());

        const std::filesystem::path masterDat = path / ResourcePaths::Dat::MASTER;
        if (std::filesystem::exists(masterDat) && std::filesystem::is_regular_file(masterDat)) {
            addDataPath(masterDat);
        }

        const std::filesystem::path critterDat = path / ResourcePaths::Dat::CRITTER;
        if (std::filesystem::exists(critterDat) && std::filesystem::is_regular_file(critterDat)) {
            addDataPath(critterDat);
        }
    } else if (path.extension() == ".dat") {
        fileSystem = std::shared_ptr<geck::GeckDat2FileSystem>(new geck::GeckDat2FileSystem(path.string()));
    } else {
        spdlog::error("Unsupported data location: {}", path.string());
        return;
    }

    fileSystem->Initialize();
    if (!fileSystem->IsInitialized()) {
        spdlog::error("Failed to initialize data path: {}", path.string());
        return;
    }

    _vfs->AddFileSystem("/", fileSystem);
    spdlog::info("Location '{}' was added to the data path", path.string());
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

    std::string normalizedPath = normalizeVfsPath(path).generic_string();
    if (!normalizedPath.empty() && normalizedPath.front() == '/') {
        normalizedPath.erase(normalizedPath.begin());
    }

    const auto& fileSystems = _vfs->GetFilesystems("/");
    for (auto it = fileSystems.rbegin(); it != fileSystems.rend(); ++it) {
        const auto& fileSystem = *it;
        if (!fileSystem || !fileSystem->IsInitialized()) {
            continue;
        }

        const vfspp::FileInfo fileInfo(fileSystem->BasePath(), normalizedPath, false);
        if (!fileSystem->IsFileExists(fileInfo)) {
            continue;
        }

        if (auto datFs = std::dynamic_pointer_cast<geck::GeckDat2FileSystem>(fileSystem)) {
            const std::filesystem::path datPath(datFs->getDatPath());
            std::string label = datPath.filename().string();
            if (label.empty()) {
                label = datPath.generic_string();
            }
            return MountedSourceInfo { MountedSourceInfo::Kind::Dat, datPath, "DAT (" + label + ")" };
        }

        if (auto nativeFs = std::dynamic_pointer_cast<vfspp::NativeFileSystem>(fileSystem)) {
            const std::filesystem::path nativePath(nativeFs->BasePath());
            std::string label = nativePath.filename().string();
            if (label.empty()) {
                label = nativePath.generic_string();
            }
            return MountedSourceInfo { MountedSourceInfo::Kind::Directory, nativePath, "Native (" + label + ")" };
        }

        return MountedSourceInfo { MountedSourceInfo::Kind::Directory, std::filesystem::path(fileSystem->BasePath()), "VFS" };
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
