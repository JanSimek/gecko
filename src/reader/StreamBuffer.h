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
 * An abstract data stream for binary resource files loaded from either Dat file or a file system
 *
 * @author alexeevdv / Falltergeist
 * @link https://github.com/falltergeist/falltergeist
 */
class StreamBuffer : public std::streambuf {
private:
    // A thin wrapper over plain C-array.
    // Handles allocation and deallocation of the underlying buffer.
    // Does not perform any kind of initialization of allocated memory.
    class Buffer {
    public:
        // Creates new empty buffer
        Buffer()
            : _size(0)
            , _buf(nullptr) {
        }

        // Creates new buffer with given size
        Buffer(size_t size)
            : _size(size) {
            _buf = new char[size];
        }

        // Constructs by moving buffer pointer from another Buffer object
        Buffer(Buffer&& other)
            : _size(other._size)
            , _buf(other._buf) {
            other._size = 0;
            other._buf = nullptr;
        }

        // Move-assigns buffer pointer from another Buffer object
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

        // Access element at a given index. No bounds checking is performed.
        char& operator[](size_t index) {
            return _buf[index];
        }

        // Access element at a given index. No bounds checking is performed.
        const char& operator[](size_t index) const {
            return _buf[index];
        }

        char* begin() {
            return &_buf[0];
        }

        char* end() {
            return &_buf[_size];
        }

        // Reallocate the underlying buffer to the specified size
        // All data in buffer will be discarded
        void resize(size_t newSize) {
            _cleanUpBuffer();
            _size = newSize;
            if (newSize > 0) {
                _buf = new char[newSize];
            } else {
                _buf = nullptr;
            }
        }

        // The current size of data in buffer
        size_t size() const {
            return _size;
        }

        // Returns true if the buffer is currently empty
        bool empty() const {
            return _size == 0;
        }

        // The pointer to underlying buffer
        char* data() {
            return _buf;
        }

        // The const pointer to underlying buffer
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
