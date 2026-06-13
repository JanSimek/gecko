#ifndef GECK_MAPPER_DAT2FILESYSTEM_HPP
#define GECK_MAPPER_DAT2FILESYSTEM_HPP

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <vfspp/IFile.h>
#include <vfspp/IFileSystem.h>
#include <vfspp/ThreadingPolicy.hpp>

#include "Dat2File.hpp"
#include "format/dat/Dat.h"
#include "reader/dat/DatReader.h"

// vfspp's IFileSystem declares CreateFile / CopyFile. On Windows <windows.h>
// (pulled in transitively by the headers above) rewrites those names to ...A,
// which breaks the overrides below. Undo the macros *after* the includes so our
// method names still match the base class.
#ifdef _WIN32
#ifdef CreateFile
#undef CreateFile
#endif
#ifdef CopyFile
#undef CopyFile
#endif
#endif

namespace geck {
namespace fs = std::filesystem;

using GeckDat2FileSystemPtr = std::shared_ptr<class GeckDat2FileSystem>;
using GeckDat2FileSystemWeakPtr = std::weak_ptr<class GeckDat2FileSystem>;

// Mounts a Fallout 2 DAT2 archive into the vfspp virtual filesystem. Entries are
// keyed by their virtual path (alias + forward-slashed archive path) and served
// read-only through Dat2File.
class GeckDat2FileSystem final : public vfspp::IFileSystem {
public:
    GeckDat2FileSystem(const std::string& aliasPath, const std::string& datPath)
        : m_AliasPath(aliasPath)
        , m_DatPath(datPath)
        , m_datReader(std::make_shared<geck::DatReader>()) {
    }

    ~GeckDat2FileSystem() {
        Shutdown();
    }

    const std::string& getDatPath() const {
        return m_DatPath;
    }

    [[nodiscard]] bool Initialize() override {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return InitializeImpl();
    }

    void Shutdown() override {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        ShutdownImpl();
    }

    [[nodiscard]] bool IsInitialized() const override {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return m_IsInitialized;
    }

    [[nodiscard]] const std::string& BasePath() const override {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return m_BasePath;
    }

    [[nodiscard]] const std::string& VirtualPath() const override {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return m_AliasPath;
    }

    [[nodiscard]] vfspp::IFileSystem::FilesList GetFilesList() const override {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return GetFilesListImpl();
    }

    [[nodiscard]] bool IsReadOnly() const override {
        return true;
    }

    vfspp::IFilePtr OpenFile(const std::string& virtualPath, vfspp::IFile::FileMode mode) override {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return OpenFileImpl(virtualPath, mode);
    }

    vfspp::IFilePtr CreateFile(const std::string& /*virtualPath*/) override {
        return nullptr;
    }

    bool RemoveFile(const std::string& /*virtualPath*/) override {
        return false;
    }

    bool CopyFile(const std::string& /*srcVirtualPath*/, const std::string& /*dstVirtualPath*/, bool /*overwrite*/ = false) override {
        return false;
    }

    bool RenameFile(const std::string& /*srcVirtualPath*/, const std::string& /*dstVirtualPath*/) override {
        return false;
    }

    void CloseFile(vfspp::IFilePtr file) override {
        if (file) {
            file->Close();
        }
    }

    [[nodiscard]] bool IsFileExists(const std::string& virtualPath) const override {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return m_Files.find(virtualPath) != m_Files.end();
    }

private:
    struct FileEntry {
        vfspp::FileInfo Info;
        std::shared_ptr<geck::DatEntry> Entry;

        FileEntry(const vfspp::FileInfo& info, std::shared_ptr<geck::DatEntry> entry)
            : Info(info)
            , Entry(std::move(entry)) {
        }
    };

    bool InitializeImpl() {
        if (m_IsInitialized) {
            return true;
        }

        if (!fs::is_regular_file(m_DatPath)) {
            return false;
        }

        m_DatArchive = m_datReader->openFile(m_DatPath);
        if (!m_DatArchive) {
            return false;
        }

        BuildFilelist();
        m_IsInitialized = true;
        return true;
    }

    void ShutdownImpl() {
        m_DatPath = "";
        m_Files.clear();
        m_DatArchive.reset();
        m_IsInitialized = false;
    }

    void BuildFilelist() {
        for (const auto& datEntry : m_DatArchive->getEntries()) {
            // DAT2 stores backslash paths; normalise to forward slashes so the
            // computed VirtualPath() matches the VFS lookup keys.
            std::string name = datEntry.first;
            std::replace(name.begin(), name.end(), '\\', '/');

            vfspp::FileInfo fileInfo(m_AliasPath, m_BasePath, name);
            m_Files.emplace(fileInfo.VirtualPath(), FileEntry(fileInfo, datEntry.second));
        }
    }

    vfspp::IFileSystem::FilesList GetFilesListImpl() const {
        vfspp::IFileSystem::FilesList fileList;
        fileList.reserve(m_Files.size());
        for (const auto& [path, entry] : m_Files) {
            fileList.push_back(entry.Info);
        }
        return fileList;
    }

    vfspp::IFilePtr OpenFileImpl(const std::string& virtualPath, vfspp::IFile::FileMode mode) {
        const auto it = m_Files.find(virtualPath);
        if (it == m_Files.end()) {
            return nullptr;
        }

        auto file = std::make_shared<Dat2File>(it->second.Info, it->second.Entry, m_datReader, m_readerMutex);
        if (!file->Open(mode)) {
            return nullptr;
        }
        return file;
    }

private:
    std::string m_AliasPath;
    std::string m_BasePath; // empty: DAT entries are addressed purely by alias
    std::string m_DatPath;
    std::shared_ptr<geck::Dat> m_DatArchive;
    std::shared_ptr<geck::DatReader> m_datReader;
    // Passed by reference to every Dat2File this archive hands out; serialises seek+read on
    // m_datReader so background map loading and UI-thread thumbnail rendering don't corrupt
    // each other. The filesystem outlives the file handles it creates.
    std::mutex m_readerMutex;
    bool m_IsInitialized = false;
    std::unordered_map<std::string, FileEntry> m_Files;
    mutable std::mutex m_Mutex;
};

} // namespace geck

#endif // GECK_MAPPER_DAT2FILESYSTEM_HPP
