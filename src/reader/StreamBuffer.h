#ifndef GECK_MAPPER_STREAMBUFFER_H
#define GECK_MAPPER_STREAMBUFFER_H

#include <fstream>
#include <string>
#include <memory>
#include <vector>

#include "format/dat/DatEntry.h"

namespace geck {

class DatReader;

/**
 * @brief A data stream for binary resource files loaded from either a Dat file or the file system.
 *
 * @author alexeevdv / Falltergeist
 * @link https://github.com/falltergeist/falltergeist
 */
class StreamBuffer : public std::streambuf {
private:
    /// Thin RAII wrapper over a plain C-array. Owns allocation/deallocation but
    /// does not initialize the allocated memory.
    class Buffer {
    public:
        Buffer()
            : _size(0)
            , _buf(nullptr) {
        }

        Buffer(size_t size)
            : _size(size) {
            _buf = new char[size];
        }

        Buffer(Buffer&& other)
            : _size(other._size)
            , _buf(other._buf) {
            other._size = 0;
            other._buf = nullptr;
        }

        Buffer& operator=(Buffer&& other) {
            _cleanUpBuffer();
            _size = other._size;
            _buf = other._buf;
            other._size = 0;
            other._buf = nullptr;
            return *this;
        }

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        ~Buffer() {
            _cleanUpBuffer();
        }

        /// Indexed access; no bounds checking is performed.
        char& operator[](size_t index) {
            return _buf[index];
        }

        /// Indexed access; no bounds checking is performed.
        const char& operator[](size_t index) const {
            return _buf[index];
        }

        char* begin() {
            return &_buf[0];
        }

        char* end() {
            return &_buf[_size];
        }

        // Reallocates to newSize; existing data is discarded.
        void resize(size_t newSize) {
            _cleanUpBuffer();
            _size = newSize;
            if (newSize > 0) {
                _buf = new char[newSize];
            } else {
                _buf = nullptr;
            }
        }

        size_t size() const {
            return _size;
        }

        bool empty() const {
            return _size == 0;
        }

        char* data() {
            return _buf;
        }

        const char* data() const {
            return _buf;
        }

    private:
        size_t _size;
        char* _buf;

        void _cleanUpBuffer() {
            delete[] _buf;
        }
    };

public:
    enum class ENDIANNESS : char {
        BIG = 0,
        LITTLE
    };
    StreamBuffer() = default;
    StreamBuffer(std::ifstream& stream, ENDIANNESS endianness = ENDIANNESS::BIG);
    StreamBuffer(std::vector<uint8_t> data, ENDIANNESS endianness = ENDIANNESS::BIG);

    virtual std::streambuf::int_type underflow();

    StreamBuffer& read(uint8_t* destination, size_t size);
    StreamBuffer& skipBytes(size_t numberOfBytes);
    StreamBuffer& setPosition(size_t position);
    size_t position() const;
    size_t size() const;

    size_t bytesRemains();

    ENDIANNESS endianness();
    void setEndianness(ENDIANNESS value);

    uint32_t uint32();
    int32_t int32();
    uint16_t uint16();
    int16_t int16();
    uint8_t uint8();
    int8_t int8();

    StreamBuffer& operator>>(uint32_t& value);
    StreamBuffer& operator>>(int32_t& value);
    StreamBuffer& operator>>(uint16_t& value);
    StreamBuffer& operator>>(int16_t& value);
    StreamBuffer& operator>>(uint8_t& value);
    StreamBuffer& operator>>(int8_t& value);

    std::string readString(size_t len);

private:
    Buffer _buffer;
    ENDIANNESS _endianness = ENDIANNESS::BIG;
};

} // geck

#endif // GECK_MAPPER_STREAMBUFFER_H
