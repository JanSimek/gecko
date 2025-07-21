#include "VirtualFileSystemExtended.h"
#include <algorithm>
#include <fnmatch.h>
#include <unordered_set>
#include <spdlog/spdlog.h>

namespace geck {

VirtualFileSystemExtended::VirtualFileSystemExtended()
    : _vfs(std::make_shared<vfspp::VirtualFileSystem>()) {
}

// ========================================
// Extended Methods (New Functionality)
// ========================================

std::vector<std::string> VirtualFileSystemExtended::listAllFiles() const {
    std::vector<std::string> allFiles;
    std::unordered_set<std::string> seenFiles; // Track files to handle priority (latest filesystem wins)
    
    try {
        spdlog::debug("VirtualFileSystemExtended: Listing all files from {} registered filesystems", 
                     _registeredFilesystems.size());
        
        // Iterate through registered filesystems in reverse order (latest first)
        // This ensures files from later registered filesystems override earlier ones
        for (auto it = _registeredFilesystems.rbegin(); it != _registeredFilesystems.rend(); ++it) {
            const auto& [alias, filesystem] = *it;
            
            if (!filesystem) {
                continue;
            }
            
            try {
                // Get the file list directly from the filesystem using the proper VFSPP API
                const auto& fileList = filesystem->FileList();
                
                for (const auto& [path, filePtr] : fileList) {
                    if (filePtr && !filePtr->GetFileInfo().IsDir()) {
                        // Construct the full aliased path
                        std::string fullPath = alias;
                        if (!alias.empty() && alias.back() == '/' && !path.empty() && path.front() == '/') {
                            // Avoid double slashes
                            fullPath += path.substr(1);
                        } else if (!alias.empty() && alias.back() != '/' && !path.empty() && path.front() != '/') {
                            // Add missing slash
                            fullPath += "/" + path;
                        } else {
                            fullPath += path;
                        }
                        
                        // Only add if we haven't seen this file before (priority: latest filesystem wins)
                        if (seenFiles.find(fullPath) == seenFiles.end()) {
                            seenFiles.insert(fullPath);
                            allFiles.push_back(fullPath);
                        }
                    }
                }
                
                spdlog::debug("VirtualFileSystemExtended: Processed {} files from filesystem '{}'", 
                             fileList.size(), alias);
                
            } catch (const std::exception& e) {
                spdlog::warn("VirtualFileSystemExtended: Error processing filesystem '{}': {}", 
                            alias, e.what());
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("VirtualFileSystemExtended: Error listing files: {}", e.what());
    }
    
    // Sort alphabetically for consistent output
    std::sort(allFiles.begin(), allFiles.end());
    
    spdlog::debug("VirtualFileSystemExtended: Found {} unique files", allFiles.size());
    return allFiles;
}

std::vector<std::string> VirtualFileSystemExtended::listFilesByPattern(const std::string& pattern) const {
    auto allFiles = listAllFiles();
    std::vector<std::string> matchingFiles;
    
    for (const auto& file : allFiles) {
        if (matchesGlobPattern(file, pattern)) {
            matchingFiles.push_back(file);
        }
    }
    
    spdlog::debug("VirtualFileSystemExtended: Found {} files matching pattern '{}'", 
                 matchingFiles.size(), pattern);
    return matchingFiles;
}

std::unordered_map<std::string, std::vector<std::string>> VirtualFileSystemExtended::listFilesByFilesystem() const {
    std::unordered_map<std::string, std::vector<std::string>> filesByFs;
    
    try {
        spdlog::debug("VirtualFileSystemExtended: Organizing files by {} registered filesystems", 
                     _registeredFilesystems.size());
        
        // Iterate through registered filesystems in registration order
        for (const auto& [alias, filesystem] : _registeredFilesystems) {
            if (!filesystem) {
                continue;
            }
            
            try {
                std::vector<std::string> files;
                
                // Get the file list directly from the filesystem using the proper VFSPP API
                const auto& fileList = filesystem->FileList();
                
                for (const auto& [path, filePtr] : fileList) {
                    if (filePtr && !filePtr->GetFileInfo().IsDir()) {
                        // Construct the full aliased path
                        std::string fullPath = alias;
                        if (!alias.empty() && alias.back() == '/' && !path.empty() && path.front() == '/') {
                            // Avoid double slashes
                            fullPath += path.substr(1);
                        } else if (!alias.empty() && alias.back() != '/' && !path.empty() && path.front() != '/') {
                            // Add missing slash
                            fullPath += "/" + path;
                        } else {
                            fullPath += path;
                        }
                        
                        files.push_back(fullPath);
                    }
                }
                
                if (!files.empty()) {
                    // Sort files alphabetically within each filesystem
                    std::sort(files.begin(), files.end());
                    filesByFs[alias] = std::move(files);
                    spdlog::debug("VirtualFileSystemExtended: Organized {} files from filesystem '{}'", 
                                 files.size(), alias);
                }
                
            } catch (const std::exception& e) {
                spdlog::warn("VirtualFileSystemExtended: Error organizing files from filesystem '{}': {}", 
                            alias, e.what());
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("VirtualFileSystemExtended: Error organizing files by filesystem: {}", e.what());
    }
    
    return filesByFs;
}

std::vector<std::string> VirtualFileSystemExtended::getMountedFilesystems() const {
    std::vector<std::string> mountedFs;
    
    // Return aliases from registered filesystems in registration order
    for (const auto& [alias, filesystem] : _registeredFilesystems) {
        if (filesystem) {
            mountedFs.push_back(alias);
        }
    }
    
    spdlog::debug("VirtualFileSystemExtended: Found {} mounted filesystems", mountedFs.size());
    return mountedFs;
}

// ========================================
// Delegated Methods (Original VFS API)
// ========================================

void VirtualFileSystemExtended::AddFileSystem(std::string alias, vfspp::IFileSystemPtr filesystem) {
    // Store in our registry for proper file listing and priority handling
    _registeredFilesystems.emplace_back(alias, filesystem);
    
    // Also add to the underlying VFSPP
    _vfs->AddFileSystem(std::move(alias), std::move(filesystem));
    
    spdlog::debug("VirtualFileSystemExtended: Registered filesystem '{}' (total: {})", 
                 alias, _registeredFilesystems.size());
}

void VirtualFileSystemExtended::RemoveFileSystem(std::string alias) {
    // Find and remove from our registry first
    auto it = std::find_if(_registeredFilesystems.begin(), _registeredFilesystems.end(),
                          [&alias](const auto& pair) { return pair.first == alias; });
    
    if (it != _registeredFilesystems.end()) {
        // Get the filesystem pointer before removing from registry
        vfspp::IFileSystemPtr filesystem = it->second;
        
        // Remove from our registry
        _registeredFilesystems.erase(it);
        
        // Remove from the underlying VFSPP (v2.0.1 requires both alias and filesystem pointer)
        if (filesystem) {
            _vfs->RemoveFileSystem(std::move(alias), filesystem);
            spdlog::debug("VirtualFileSystemExtended: Removed filesystem '{}' (remaining: {})", 
                         alias, _registeredFilesystems.size());
        } else {
            spdlog::warn("VirtualFileSystemExtended: Filesystem '{}' had null pointer in registry", alias);
        }
    } else {
        spdlog::warn("VirtualFileSystemExtended: Filesystem '{}' not found in registry", alias);
    }
}

bool VirtualFileSystemExtended::IsFileSystemExists(std::string alias) const {
    // Check our registry since VFSPP v2.0.1 doesn't have IsFileSystemExists method
    auto it = std::find_if(_registeredFilesystems.begin(), _registeredFilesystems.end(),
                          [&alias](const auto& pair) { return pair.first == alias; });
    
    bool exists = (it != _registeredFilesystems.end()) && (it->second != nullptr);
    spdlog::debug("VirtualFileSystemExtended: Filesystem '{}' exists: {}", alias, exists);
    return exists;
}

vfspp::IFileSystemPtr VirtualFileSystemExtended::GetFilesystem(std::string alias) {
    // Use our registry for consistent behavior
    auto it = std::find_if(_registeredFilesystems.begin(), _registeredFilesystems.end(),
                          [&alias](const auto& pair) { return pair.first == alias; });
    
    if (it != _registeredFilesystems.end()) {
        return it->second;
    }
    return nullptr;
}

vfspp::IFileSystemPtr VirtualFileSystemExtended::GetFilesystem(std::string alias) const {
    // Use our registry for consistent behavior
    auto it = std::find_if(_registeredFilesystems.begin(), _registeredFilesystems.end(),
                          [&alias](const auto& pair) { return pair.first == alias; });
    
    if (it != _registeredFilesystems.end()) {
        return it->second;
    }
    return nullptr;
}

vfspp::IFilePtr VirtualFileSystemExtended::OpenFile(const vfspp::FileInfo& filePath, vfspp::IFile::FileMode mode) {
    return _vfs->OpenFile(filePath, mode);
}

void VirtualFileSystemExtended::CloseFile(vfspp::IFilePtr file) {
    // Note: CloseFile method doesn't exist in VFSPP v2.0.1 VirtualFileSystem
    // Files are closed automatically when the IFilePtr goes out of scope
    if (file) {
        file->Close();
    }
}

// ========================================
// Private Helper Methods
// ========================================

bool VirtualFileSystemExtended::matchesGlobPattern(const std::string& filename, const std::string& pattern) const {
    // Use fnmatch for glob pattern matching
    return fnmatch(pattern.c_str(), filename.c_str(), FNM_PATHNAME) == 0;
}

void VirtualFileSystemExtended::collectFilesFromFilesystem(const std::string& alias, 
                                                         vfspp::IFileSystemPtr filesystem,
                                                         std::vector<std::string>& files) const {
    if (!filesystem) {
        return;
    }
    
    try {
        // Get the file list from the filesystem
        const auto& fileList = filesystem->FileList();
        
        for (const auto& [path, filePtr] : fileList) {
            if (filePtr && !filePtr->GetFileInfo().IsDir()) {
                // Construct the full aliased path
                std::string fullPath = alias;
                if (!alias.empty() && alias.back() == '/' && !path.empty() && path.front() == '/') {
                    // Avoid double slashes
                    fullPath += path.substr(1);
                } else if (!alias.empty() && alias.back() != '/' && !path.empty() && path.front() != '/') {
                    // Add missing slash
                    fullPath += "/" + path;
                } else {
                    fullPath += path;
                }
                
                files.push_back(fullPath);
            }
        }
        
        spdlog::debug("VirtualFileSystemExtended: Collected {} files from filesystem '{}'", 
                     fileList.size(), alias);
        
    } catch (const std::exception& e) {
        spdlog::warn("VirtualFileSystemExtended: Error collecting files from filesystem '{}': {}", 
                    alias, e.what());
    }
}

} // namespace geck