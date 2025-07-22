#ifndef GECK_MAPPER_DAT2FILE_HPP
#define GECK_MAPPER_DAT2FILE_HPP

#include <cstring>
#include <zlib.h>

#include <vfspp/IFile.h>

#include "format/dat/Dat.h"
#include "reader/dat/DatReader.h"

namespace geck {
class Dat2File final : public vfspp::IFile {

public:
    Dat2File(const vfspp::FileInfo& fileInfo,
        const std::shared_ptr<geck::DatEntry>& datEntry,
        const std::shared_ptr<geck::DatReader>& datReader)
        : m_FileInfo(fileInfo)
        , m_datEntry(datEntry)
        , m_datReader(datReader)
        , m_IsOpened(false)
        , m_SeekPos(0)
    {
    }

    ~Dat2File()
    {
        Close();
    }

    /*
     * Get file information
     */
    virtual const vfspp::FileInfo& GetFileInfo() const override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return GetFileInfoST();
        } else {
            return GetFileInfoST();
        }
    }

    /*
     * Returns file size
     */
    virtual uint64_t Size() override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return SizeST();
        } else {
            return SizeST();
        }
    }

    /*
     * Check is readonly filesystem
     */
    virtual bool IsReadOnly() const override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return IsReadOnlyST();
        } else {
            return IsReadOnlyST();
        }
    }

    /*
     * Open file for reading/writting
     */
    virtual void Open(FileMode mode) override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            OpenST(mode);
        } else {
            OpenST(mode);
        }
    }

    /*
     * Close file
     */
    virtual void Close() override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            CloseST();
        } else {
            CloseST();
        }
    }

    /*
     * Check is file ready for reading/writing
     */
    virtual bool IsOpened() const override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return IsOpenedST();
        } else {
            return IsOpenedST();
        }
    }

    /*
     * Seek on a file
     */
    virtual uint64_t Seek(uint64_t offset, Origin origin) override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return SeekST(offset, origin);
        } else {
            return SeekST(offset, origin);
        }
    }
    /*
     * Returns offset in file
     */
    virtual uint64_t Tell() override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return TellST();
        } else {
            return TellST();
        }
    }

    /*
     * Read data from file to buffer
     */
    virtual uint64_t Read(uint8_t* buffer, uint64_t size) override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return ReadST(buffer, size);
        } else {
            return ReadST(buffer, size);
        }
    }
    /*
     * Write buffer data to file
     */
    virtual uint64_t Write(const uint8_t* buffer, uint64_t size) override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return WriteST(buffer, size);
        } else {
            return WriteST(buffer, size);
        }
    }

    /*
     * Read data from file to vector
     */
    virtual uint64_t Read(std::vector<uint8_t>& buffer, uint64_t size) override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return ReadST(buffer, size);
        } else {
            return ReadST(buffer, size);
        }
    }

    /*
     * Write data from vector to file
     */
    virtual uint64_t Write(const std::vector<uint8_t>& buffer) override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return WriteST(buffer);
        } else {
            return WriteST(buffer);
        }
    }

    /*
     * Read data from file to stream
     */
    virtual uint64_t Read(std::ostream& stream, uint64_t size, uint64_t bufferSize = 1024) override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return ReadST(stream, size, bufferSize);
        } else {
            return ReadST(stream, size, bufferSize);
        }
    }

    /*
     * Write data from stream to file
     */
    virtual uint64_t Write(std::istream& stream, uint64_t size, uint64_t bufferSize = 1024) override
    {
        if constexpr (VFSPP_MT_SUPPORT_ENABLED) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return WriteST(stream, size, bufferSize);
        } else {
            return WriteST(stream, size, bufferSize);
        }
    }

private:
    inline const vfspp::FileInfo& GetFileInfoST() const
    {
        return m_FileInfo;
    }

    inline uint64_t SizeST()
    {
        if (IsOpenedST()) {
            return m_datEntry->getDecompressedSize();
        }

        return 0;
    }

    inline bool IsReadOnlyST() const
    {
        return true;
    }

    inline void OpenST(FileMode mode)
    {
        bool requestWrite = ((mode & IFile::FileMode::Write) == IFile::FileMode::Write);
        requestWrite |= ((mode & IFile::FileMode::Append) == IFile::FileMode::Append);
        requestWrite |= ((mode & IFile::FileMode::Truncate) == IFile::FileMode::Truncate);

        if (IsReadOnlyST() && requestWrite) {
            return;
        }

        if (IsOpenedST()) {
            SeekST(0, IFile::Origin::Begin);
            return;
        }

        m_SeekPos = 0;

        m_Data.resize(m_datEntry->getDecompressedSize());

        // Read into the buffer
        m_datReader->setPosition(m_datEntry->getOffset());
        if (m_datEntry->getCompressed()) {
            auto* packed_data = new uint8_t[m_datEntry->getPackedSize()];
            m_datReader->read_bytes(packed_data, m_datEntry->getPackedSize());

            // unpacking
            z_stream zStream;
            zStream.total_in = m_datEntry->getPackedSize();
            zStream.avail_in = m_datEntry->getPackedSize();
            zStream.next_in = packed_data;
            zStream.total_out = zStream.avail_out = m_datEntry->getDecompressedSize();
            zStream.next_out = &m_Data[0];
            zStream.zalloc = Z_NULL;
            zStream.zfree = Z_NULL;
            zStream.opaque = Z_NULL;
            inflateInit(&zStream);                    // zlib function
            inflate(&zStream, Z_FINISH);  // zlib function
            inflateEnd(&zStream);               // zlib function

            delete[] packed_data;
        } else {
            m_datReader->read_bytes(&m_Data[0], m_datEntry->getDecompressedSize());
        }

        m_IsOpened = true;
    }

    inline void CloseST()
    {
        m_IsOpened = false;
        m_SeekPos = 0;

        m_Data.clear();
    }

    inline bool IsOpenedST() const
    {
        return m_IsOpened;
    }

    inline uint64_t SeekST(uint64_t offset, Origin origin)
    {
        if (!IsOpenedST()) {
            return 0;
        }

        if (origin == IFile::Origin::Begin) {
            m_SeekPos = offset;
        } else if (origin == IFile::Origin::End) {
            m_SeekPos = SizeST() - offset;
        } else if (origin == IFile::Origin::Set) {
            m_SeekPos += offset;
        }
        m_SeekPos = std::min(m_SeekPos, SizeST() - 1);

        return TellST();
    }

    inline uint64_t TellST()
    {
        return m_SeekPos;
    }

    inline uint64_t ReadST(uint8_t* buffer, uint64_t size)
    {
        if (!IsOpenedST()) {
            return 0;
        }

        memcpy(buffer, &m_Data[0], static_cast<size_t>(size));

        uint64_t leftSize = SizeST() - TellST();
        uint64_t maxSize = std::min(size, leftSize);
        if (maxSize > 0) {
            memcpy(buffer, &m_Data[0], static_cast<size_t>(maxSize));
            return maxSize;
        }

        return 0;
    }

    inline uint64_t WriteST([[maybe_unused]] const uint8_t* buffer, [[maybe_unused]] uint64_t size)
    {
        return 0;
    }

    inline uint64_t ReadST(std::vector<uint8_t>& buffer, uint64_t size)
    {
        buffer.resize(size);
        return ReadST(buffer.data(), size);
    }

    inline uint64_t WriteST(const std::vector<uint8_t>& buffer)
    {
        return WriteST(buffer.data(), buffer.size());
    }

    inline uint64_t ReadST(std::ostream& stream, uint64_t size, uint64_t bufferSize = 1024)
    {
        // read chunk of data from file and write it to stream until all data is read
        uint64_t totalSize = size;
        std::vector<uint8_t> buffer(bufferSize);
        while (size > 0) {
            uint64_t bytesRead = ReadST(buffer.data(), std::min(size, static_cast<uint64_t>(buffer.size())));
            if (bytesRead == 0) {
                break;
            }

            if (size < bytesRead) {
                bytesRead = size;
            }

            stream.write(reinterpret_cast<char*>(buffer.data()), bytesRead);

            size -= bytesRead;
        }

        return totalSize - size;
    }

    inline uint64_t WriteST(std::istream& stream, uint64_t size, uint64_t bufferSize = 1024)
    {
        // write chunk of data from stream to file untill all data is written
        uint64_t totalSize = size;
        std::vector<uint8_t> buffer(bufferSize);
        while (size > 0) {
            stream.read(reinterpret_cast<char*>(buffer.data()), std::min(size, static_cast<uint64_t>(buffer.size())));
            uint64_t bytesRead = stream.gcount();
            if (bytesRead == 0) {
                break;
            }

            if (size < bytesRead) {
                bytesRead = size;
            }

            WriteST(buffer.data(), bytesRead);

            size -= bytesRead;
        }

        return totalSize - size;
    }

private:
    vfspp::FileInfo m_FileInfo;
    std::vector<uint8_t> m_Data;
    std::shared_ptr<geck::DatEntry> m_datEntry;
    std::shared_ptr<geck::DatReader> m_datReader;
    bool m_IsOpened;
    uint64_t m_SeekPos;
    mutable std::mutex m_Mutex;
};
} // geck
#endif // GECK_MAPPER_DAT2FILE_HPP
