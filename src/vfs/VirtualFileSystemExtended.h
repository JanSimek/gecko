#ifndef GECK_MAPPER_VIRTUALFILESYSTEMEXTENDED_H
#define GECK_MAPPER_VIRTUALFILESYSTEMEXTENDED_H

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <vfspp/VirtualFileSystem.hpp>
#include <vfspp/IFileSystem.h>
#include <vfspp/FileInfo.hpp>

namespace geck {

using VirtualFileSystemExtendedPtr = std::shared_ptr<class VirtualFileSystemExtended>;
using VirtualFileSystemExtendedWeakPtr = std::weak_ptr<class VirtualFileSystemExtended>;

/**
 * @brief Extended wrapper around vfspp::VirtualFileSystem that adds file listing functionality
 * 
 * This class wraps the third-party VFSPP VirtualFileSystem and adds additional methods
 * for listing all files across mounted filesystems without modifying the original library.
 * 
 * Features:
 * - List all files across all mounted filesystems
 * - Filter files by glob patterns
 * - Organize files by source filesystem
 * - Full API compatibility with original VirtualFileSystem
 * - Thread-safe implementation
 */
class VirtualFileSystemExtended final {
public:
    VirtualFileSystemExtended();
    ~VirtualFileSystemExtended() = default;
    
    // ========================================
    // Extended Methods (New Functionality)
    // ========================================
    
    /**
     * @brief List all files from all mounted filesystems
     * @return Vector of all file paths (absolute paths with aliases)
     */
    std::vector<std::string> listAllFiles() const;
    
    /**
     * @brief List files matching a glob pattern
     * @param pattern Glob pattern (e.g., "*.lst", "art/items/")
     * @return Vector of matching file paths
     */
    std::vector<std::string> listFilesByPattern(const std::string& pattern) const;
    
    /**
     * @brief List files organized by source filesystem
     * @return Map of filesystem alias to vector of file paths
     */
    std::unordered_map<std::string, std::vector<std::string>> listFilesByFilesystem() const;
    
    /**
     * @brief Get all mounted filesystem aliases
     * @return Vector of filesystem aliases (mount points)
     */
    std::vector<std::string> getMountedFilesystems() const;
    
    // ========================================
    // Delegated Methods (Original VFS API)
    // ========================================
    
    /**
     * @brief Register new filesystem with alias
     * @param alias Base prefix for file access
     * @param filesystem Filesystem to mount
     */
    void AddFileSystem(std::string alias, vfspp::IFileSystemPtr filesystem);
    
    /**
     * @brief Remove registered filesystem
     * @param alias Filesystem alias to remove
     */
    void RemoveFileSystem(std::string alias);
    
    /**
     * @brief Check if filesystem with alias exists
     * @param alias Filesystem alias to check
     * @return True if filesystem is registered
     */
    bool IsFileSystemExists(std::string alias) const;
    
    /**
     * @brief Get filesystem by alias
     * @param alias Filesystem alias
     * @return Filesystem pointer or nullptr if not found
     */
    vfspp::IFileSystemPtr GetFilesystem(std::string alias);
    vfspp::IFileSystemPtr GetFilesystem(std::string alias) const;
    
    /**
     * @brief Open file from any mounted filesystem
     * @param filePath File path with alias
     * @param mode File open mode
     * @return File pointer or nullptr if not found
     */
    vfspp::IFilePtr OpenFile(const vfspp::FileInfo& filePath, vfspp::IFile::FileMode mode);
    
    /**
     * @brief Close opened file
     * @param file File to close
     */
    void CloseFile(vfspp::IFilePtr file);
    
    /**
     * @brief Get the underlying VFS instance (for compatibility)
     * @return Raw pointer to wrapped VFS
     */
    vfspp::VirtualFileSystem* getUnderlyingVFS() const { return _vfs.get(); }
    
private:
    vfspp::VirtualFileSystemPtr _vfs;
    
    // Registry to track filesystems in order of registration for proper file priority
    // Later registered filesystems override files from earlier ones
    std::vector<std::pair<std::string, vfspp::IFileSystemPtr>> _registeredFilesystems;
    
    // Helper methods
    bool matchesGlobPattern(const std::string& filename, const std::string& pattern) const;
    void collectFilesFromFilesystem(const std::string& alias, 
                                   vfspp::IFileSystemPtr filesystem,
                                   std::vector<std::string>& files) const;
};

} // namespace geck

#endif // GECK_MAPPER_VIRTUALFILESYSTEMEXTENDED_H