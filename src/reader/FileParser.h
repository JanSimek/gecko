#pragma once

#include <memory>
#include <filesystem>
#include <spdlog/spdlog.h>

#include "StreamBuffer.h"

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

    std::unique_ptr<T> openFile(const std::filesystem::path& filename, const std::vector<uint8_t>& data) {
        _stream = StreamBuffer(data);
        this->_path = filename.string();

        return read();
    }

    std::unique_ptr<T> openFile(const std::filesystem::path& path,
        std::ios_base::openmode mode = std::ifstream::in | std::ifstream::binary) {
        std::ifstream stream{ path.string(), mode };

        if (!stream.is_open()) {
            throw std::runtime_error{ "Could not read file " + path.string() };
        }

        _stream = StreamBuffer(stream, _endianness);
        this->_path = path;
        return read();
    }

    virtual std::unique_ptr<T> read() = 0;

    inline uint32_t read_u8() {
        return _stream.uint8();
    }

    inline uint32_t read_le_u32() {
        return _stream.uint32();
    }

    inline uint8_t read_be_u8() {
        return _stream.uint8();
    }

    inline uint16_t read_be_u16() {
        return _stream.uint16();
    }

    inline uint32_t read_be_u32() {
        return _stream.uint32();
    }

    inline int32_t read_be_i32() {
        return read_be_u32();
    }

    inline std::string read_str(size_t len) {
        return _stream.readString(len);
    }

    inline void read_bytes(uint8_t* buf, size_t n) {
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
};

} // namespace geck