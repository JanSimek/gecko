#pragma once

#include <memory>
#include <filesystem>
#include <spdlog/spdlog.h>

#include "StreamBuffer.h"
#include "ReaderExceptions.h"

namespace geck {

template <typename T>
class FileParser {
protected:
    StreamBuffer _stream;
    std::filesystem::path _path;
    StreamBuffer::ENDIANNESS _endianness = StreamBuffer::ENDIANNESS::BIG;

public:
    FileParser() = default;
    explicit FileParser(StreamBuffer::ENDIANNESS endianness)
        : _endianness(endianness) { }
    virtual ~FileParser() = default;

    std::unique_ptr<T> openFile(const std::filesystem::path& filename, const std::vector<uint8_t>& data) {
        try {
            _stream = StreamBuffer(data);
            this->_path = filename;
            
            spdlog::debug("Opening file from data: {}", filename.string());
            return read();
        } catch (const std::exception& e) {
            throw IOException("Failed to open file from data: " + std::string(e.what()), filename);
        }
    }

    std::unique_ptr<T> openFile(const std::filesystem::path& path,
        std::ios_base::openmode mode = std::ifstream::in | std::ifstream::binary) {
        try {
            std::ifstream stream{ path.string(), mode };

            if (!stream.is_open()) {
                throw IOException("Could not open file", path);
            }

            _stream = StreamBuffer(stream, _endianness);
            this->_path = path;
            
            spdlog::debug("Opening file: {}", path.string());
            return read();
        } catch (const FileReaderException&) {
            throw;
        } catch (const std::exception& e) {
            throw IOException("Failed to open file: " + std::string(e.what()), path);
        }
    }

    virtual std::unique_ptr<T> read() = 0;

    inline uint32_t read_u8() {
        validateStreamPosition(1);
        return _stream.uint8();
    }

    inline uint32_t read_le_u32() {
        validateStreamPosition(4);
        return _stream.uint32();
    }

    inline uint8_t read_be_u8() {
        validateStreamPosition(1);
        return _stream.uint8();
    }

    inline uint16_t read_be_u16() {
        validateStreamPosition(2);
        return _stream.uint16();
    }

    inline uint32_t read_be_u32() {
        validateStreamPosition(4);
        return _stream.uint32();
    }

    inline int32_t read_be_i32() {
        return read_be_u32();
    }

    inline std::string read_str(size_t len) {
        validateStreamPosition(len);
        return _stream.readString(len);
    }

    inline void read_bytes(uint8_t* buf, size_t n) {
        validateStreamPosition(n);
        _stream.read(buf, n);
    }

    inline void setPosition(size_t position) {
        _stream.setPosition(position);
    }

    template <size_t N>
    inline void skip() {
        std::array<uint8_t, N> buf;
        read_bytes(buf.data(), buf.size());
    }

protected:
    inline void validateStreamPosition(size_t requiredBytes) const {
        if (_stream.position() + requiredBytes > _stream.size()) {
            throw ParseException("Attempt to read beyond end of file", _path, _stream.position());
        }
    }

};

} // namespace geck