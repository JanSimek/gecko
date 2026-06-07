#ifndef GECK_MAPPER_DAT2FILE_HPP
#define GECK_MAPPER_DAT2FILE_HPP

#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

#include <zlib.h>

#include <vfspp/IFile.h>
#include <vfspp/ThreadingPolicy.hpp>

#include "format/dat/Dat.h"
#include "reader/dat/DatReader.h"

namespace geck {

// A single entry of a Fallout 2 DAT2 archive exposed through the vfspp IFile
// interface. The (optionally zlib-compressed) entry is inflated into memory on
// Open() and served from there. The archive is read-only.
class Dat2File final : public vfspp::IFile {
public:
    Dat2File(const vfspp::FileInfo& fileInfo,
        const std::shared_ptr<geck::DatEntry>& datEntry,
        const std::shared_ptr<geck::DatReader>& datReader)
        : m_FileInfo(fileInfo)
        , m_datEntry(datEntry)
        , m_datReader(datReader)
    {
    }

    ~Dat2File() override
    {
        Close();
    }

    [[nodiscard]] const vfspp::FileInfo& GetFileInfo() const override
    {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return m_FileInfo;
    }

    [[nodiscard]] uint64_t Size() const override
    {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return m_datEntry->getDecompressedSize();
    }

    [[nodiscard]] bool IsReadOnly() const override
    {
        return true;
    }

    [[nodiscard]] bool Open(FileMode mode) override
    {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return OpenImpl(mode);
    }

    void Close() override
    {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        m_IsOpened = false;
        m_SeekPos = 0;
        m_Data.clear();
    }

    [[nodiscard]] bool IsOpened() const override
    {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return m_IsOpened;
    }

    uint64_t Seek(uint64_t offset, Origin origin) override
    {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return SeekImpl(offset, origin);
    }

    [[nodiscard]] uint64_t Tell() const override
    {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return m_SeekPos;
    }

    uint64_t Read(std::span<uint8_t> buffer) override
    {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        return ReadImpl(buffer);
    }

    uint64_t Read(std::vector<uint8_t>& buffer, uint64_t size) override
    {
        [[maybe_unused]] auto lock = vfspp::ThreadingPolicy::Lock(m_Mutex);
        buffer.resize(size);
        return ReadImpl(std::span<uint8_t>(buffer.data(), buffer.size()));
    }

    // Read-only filesystem: writes are no-ops.
    uint64_t Write(std::span<const uint8_t> /*buffer*/) override
    {
        return 0;
    }

    uint64_t Write(const std::vector<uint8_t>& /*buffer*/) override
    {
        return 0;
    }

private:
    bool OpenImpl(FileMode mode)
    {
        if (!IFile::IsModeValid(mode)) {
            return false;
        }
        if (IFile::ModeHasFlag(mode, FileMode::Write)) {
            return false; // read-only archive
        }

        if (m_IsOpened) {
            m_SeekPos = 0;
            return true;
        }

        m_SeekPos = 0;
        m_Data.resize(m_datEntry->getDecompressedSize());

        m_datReader->setPosition(m_datEntry->getOffset());
        if (m_datEntry->getCompressed()) {
            std::vector<uint8_t> packed(m_datEntry->getPackedSize());
            m_datReader->read_bytes(packed.data(), packed.size());

            // zlib inflate the DAT entry into m_Data
            z_stream zStream {};
            zStream.next_in = packed.data();
            zStream.avail_in = static_cast<uInt>(packed.size());
            zStream.next_out = m_Data.data();
            zStream.avail_out = static_cast<uInt>(m_Data.size());
            zStream.zalloc = Z_NULL;
            zStream.zfree = Z_NULL;
            zStream.opaque = Z_NULL;
            inflateInit(&zStream);
            inflate(&zStream, Z_FINISH);
            inflateEnd(&zStream);
        } else {
            m_datReader->read_bytes(m_Data.data(), m_Data.size());
        }

        m_IsOpened = true;
        return true;
    }

    uint64_t SeekImpl(uint64_t offset, Origin origin)
    {
        if (!m_IsOpened) {
            return 0;
        }

        const uint64_t size = m_Data.size();
        if (origin == IFile::Origin::Begin) {
            m_SeekPos = offset;
        } else if (origin == IFile::Origin::End) {
            m_SeekPos = (offset <= size) ? size - offset : 0;
        } else if (origin == IFile::Origin::Set) {
            m_SeekPos += offset;
        }
        m_SeekPos = std::min(m_SeekPos, size);

        return m_SeekPos;
    }

    uint64_t ReadImpl(std::span<uint8_t> buffer)
    {
        if (!m_IsOpened || m_SeekPos >= m_Data.size()) {
            return 0;
        }

        const uint64_t bytesLeft = m_Data.size() - m_SeekPos;
        const uint64_t bytesToRead = std::min<uint64_t>(bytesLeft, buffer.size());
        if (bytesToRead == 0) {
            return 0;
        }

        std::memcpy(buffer.data(), m_Data.data() + m_SeekPos, static_cast<size_t>(bytesToRead));
        m_SeekPos += bytesToRead;
        return bytesToRead;
    }

private:
    vfspp::FileInfo m_FileInfo;
    std::vector<uint8_t> m_Data;
    std::shared_ptr<geck::DatEntry> m_datEntry;
    std::shared_ptr<geck::DatReader> m_datReader;
    bool m_IsOpened = false;
    uint64_t m_SeekPos = 0;
    mutable std::mutex m_Mutex;
};

} // namespace geck

#endif // GECK_MAPPER_DAT2FILE_HPP
