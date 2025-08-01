#ifndef GECK_MAPPER_DAT2FILESYSTEM_HPP
#define GECK_MAPPER_DAT2FILESYSTEM_HPP

// Prevent Windows API macros from interfering with method names
#ifdef _WIN32
#ifdef CreateFile
#undef CreateFile
#endif
#ifdef CopyFile
#undef CopyFile
#endif
#endif

#include <cstring>
#include <memory>
#include <mutex>
#include <filesystem>

#include <vfspp/IFile.h>
#include <vfspp/IFileSystem.h>

#include "Dat2File.hpp"
#include "format/dat/Dat.h"
#include "reader/dat/DatReader.h"

namespace geck {
namespace fs = std::filesystem;

using GeckDat2FileSystemPtr = std::shared_ptr<class GeckDat2FileSystem>;
using GeckDat2FileSystemWeakPtr = std::weak_ptr<class GeckDat2FileSystem>;

class GeckDat2FileSystem final : public vfspp::IFileSystem {
public:
    GeckDat2FileSystem(const std::string& datPath)
        : m_DatPath(datPath)
        , m_datReader(std::make_shared<geck::DatReader>())
        , m_IsInitialized(false)
    {
    }
    
    ~GeckDat2FileSystem()
    {
        Shutdown();
    }
    
    /*
     * Initialize filesystem, call this method as soon as possible
     */
    virtual void Initialize() override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            InitializeST();
        } else {
            InitializeST();
        }
    }

    /*
     * Shutdown filesystem
     */
    virtual void Shutdown() override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            ShutdownST();
        } else {
            ShutdownST();
        }
    }
    
    /*
     * Check if filesystem is initialized
     */
    virtual bool IsInitialized() const override
    {
        return m_IsInitialized;
    }
    
    /*
     * Get base path
     */
    virtual const std::string& BasePath() const override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return BasePathST();
        } else {
            return BasePathST();
        }
    }
    
    /*
     * Retrieve file list according filter
     */
    virtual const vfspp::IFileSystem::TFileList& FileList() const override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return FileListST();
        } else {
            return FileListST();
        }
    }
    
    /*
     * Check is readonly filesystem
     */
    virtual bool IsReadOnly() const override
    {
        return true;
    }
    
    /*
     * Open existing file for reading, if not exists returns null for readonly filesystem. 
     * If file not exists and filesystem is writable then create new file
     */
    virtual vfspp::IFilePtr OpenFile(const vfspp::FileInfo& filePath, vfspp::IFile::FileMode mode) override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return OpenFileST(filePath, mode);
        } else {
            return OpenFileST(filePath, mode);
        }
    }
    
    /*
     * Create file on writeable filesystem. Returns true if file created successfully
     */
    virtual bool CreateFile([[maybe_unused]] const vfspp::FileInfo& filePath) override
    {
        return false;
    }
    
    /*
     * Remove existing file on writable filesystem
     */
    virtual bool RemoveFile([[maybe_unused]] const vfspp::FileInfo& filePath) override
    {
        return false;
    }
    
    /*
     * Copy existing file on writable filesystem
     */
    virtual bool CopyFile([[maybe_unused]] const vfspp::FileInfo& src, [[maybe_unused]] const vfspp::FileInfo& dest) override
    {
        return false;
    }
    
    /*
     * Rename existing file on writable filesystem
     */
    virtual bool RenameFile([[maybe_unused]] const vfspp::FileInfo& srcPath, [[maybe_unused]] const vfspp::FileInfo& dstPath) override
    {
        return false;
    }

    /*
     * Close file on filesystem
     */
    virtual void CloseFile(vfspp::IFilePtr file) override
    {
        if (file) {
            file->Close();
        }
    }

    /*
     * Check if file exists on filesystem
     */
    virtual bool IsFileExists(const vfspp::FileInfo& filePath) const override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return IsFileExistsST(filePath);
        } else {
            return IsFileExistsST(filePath);
        }
    }

    /*
     * Check is file
     */
    virtual bool IsFile(const vfspp::FileInfo& filePath) const override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return IFileSystem::IsFile(filePath, m_FileList);
        } else {
            return IFileSystem::IsFile(filePath, m_FileList);
        }
    }
    
    /*
     * Check is dir
     */
    virtual bool IsDir(const vfspp::FileInfo& dirPath) const override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return IFileSystem::IsDir(dirPath, m_FileList);
        } else {
            return IFileSystem::IsDir(dirPath, m_FileList);
        }
    }

private:
    inline void InitializeST()
    {
        if (m_IsInitialized) {
            return;
        }

        if (!fs::is_regular_file(m_DatPath)) {
            return;
        }

        m_DatArchive = m_datReader->openFile(m_DatPath);

        BuildFilelist(m_DatArchive, m_FileList);
        m_IsInitialized = true;
    }

    inline void ShutdownST()
    {
        m_DatPath = "";
        // close all files
        for (auto& file : m_FileList) {
            file.second->Close();
        }
        m_FileList.clear();

        m_IsInitialized = false;
    }
    
    inline bool IsInitializedST() const
    {
        return m_IsInitialized;
    }

    inline const std::string& BasePathST() const
    {
        static std::string rootPath = "/";
        return rootPath;
    }

    inline const vfspp::IFileSystem::TFileList& FileListST() const
    {
        return m_FileList;
    }
    
    inline vfspp::IFilePtr OpenFileST(const vfspp::FileInfo& filePath, vfspp::IFile::FileMode mode)
    {
        // check if filesystem is readonly and mode is write then return null
        bool requestWrite = ((mode & vfspp::IFile::FileMode::Write) == vfspp::IFile::FileMode::Write);
        requestWrite |= ((mode & vfspp::IFile::FileMode::Append) == vfspp::IFile::FileMode::Append);
        requestWrite |= ((mode & vfspp::IFile::FileMode::Truncate) == vfspp::IFile::FileMode::Truncate);

        // Note 'IsReadOnly()' is safe to call on any thread
        if (IsReadOnly() && requestWrite) {
            return nullptr;
        }

        vfspp::IFilePtr file = FindFile(filePath, m_FileList);
        if (file) {
            file->Open(mode);
        }
        
        return file;
    }

    inline bool IsFileExistsST(const vfspp::FileInfo& filePath) const
    {
        return FindFile(filePath, m_FileList) != nullptr;
    }

    void BuildFilelist(const std::shared_ptr<geck::Dat>& datArchive, vfspp::IFileSystem::TFileList& outFileList)
    {
        for (const auto& datEntry : datArchive->getEntries()) {
            // TODO: add directories !!!!
            // DAT entries use forward slashes (e.g., "art/tiles/tile.frm")
            // FileInfo constructor will call Configure() which uses generic_string()
            // This ensures consistent forward slash usage in the VFS layer
            vfspp::FileInfo fileInfo(BasePathST(), datEntry.first, false);
            vfspp::IFilePtr file(new Dat2File(fileInfo, datEntry.second, m_datReader));
            outFileList.insert(std::pair(file->GetFileInfo().AbsolutePath(), file));
        }
    }
    
private:
    std::string m_DatPath;
    std::shared_ptr<geck::Dat> m_DatArchive;
    std::shared_ptr<geck::DatReader> m_datReader;
    bool m_IsInitialized;
    vfspp::IFileSystem::TFileList m_FileList;
    mutable std::mutex m_Mutex;
};
} // geck
#endif // GECK_MAPPER_DAT2FILESYSTEM_HPP
