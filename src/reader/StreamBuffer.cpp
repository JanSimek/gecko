#include "StreamBuffer.h"

#include <zlib.h>
#include <cstring>
#include <algorithm>

namespace geck {

StreamBuffer::StreamBuffer(std::vector<uint8_t> data, ENDIANNESS endianness)
    : _endianness(endianness) {
    _buffer.resize(data.size());
    auto cBuf = _buffer.data();
    if (!data.empty()) { // an empty vector's data() may be null, which memcpy must not get
        memcpy(cBuf, data.data(), data.size());
    }
    setg(cBuf, cBuf, cBuf + data.size());
}

StreamBuffer::StreamBuffer(std::ifstream& stream, ENDIANNESS endianness)
    : _endianness(endianness) {
    stream.seekg(0, std::ios::end);
    auto size = static_cast<size_t>(stream.tellg());
    stream.seekg(0, std::ios::beg);

    _buffer.resize(size);
    auto cBuf = _buffer.data();
    stream.read(cBuf, size);
    setg(cBuf, cBuf, cBuf + size);
}

size_t StreamBuffer::size() const {
    return _buffer.size();
}

std::streambuf::int_type StreamBuffer::underflow() {
    if (gptr() == egptr()) {
        return traits_type::eof();
    }
    return traits_type::to_int_type(*gptr());
}

StreamBuffer& StreamBuffer::setPosition(size_t pos) {
    auto cBuf = _buffer.data();
    setg(cBuf, cBuf + pos, cBuf + _buffer.size());
    return *this;
}

size_t StreamBuffer::position() const {
    return gptr() - eback();
}

StreamBuffer& StreamBuffer::read(uint8_t* destination, size_t size) {
    sgetn((char*)destination, size);
    return *this;
}

StreamBuffer& StreamBuffer::operator>>(uint32_t& value) {
    char* buff = reinterpret_cast<char*>(&value);
    sgetn(buff, sizeof(value));
    if (endianness() == ENDIANNESS::BIG) {
        std::reverse(buff, buff + sizeof(value));
    }
    return *this;
}

StreamBuffer& StreamBuffer::operator>>(uint16_t& value) {
    char* buff = reinterpret_cast<char*>(&value);
    sgetn(buff, sizeof(value));
    if (endianness() == ENDIANNESS::BIG) {
        std::reverse(buff, buff + sizeof(value));
    }
    return *this;
}

StreamBuffer& StreamBuffer::operator>>(uint8_t& value) {
    sgetn(reinterpret_cast<char*>(&value), sizeof(value));
    return *this;
}

StreamBuffer::ENDIANNESS StreamBuffer::endianness() {
    return _endianness;
}

uint32_t StreamBuffer::uint32() {
    uint32_t value = 0;
    *this >> value;
    return value;
}

uint16_t StreamBuffer::uint16() {
    uint16_t value = 0;
    *this >> value;
    return value;
}

uint8_t StreamBuffer::uint8() {
    uint8_t value = 0;
    *this >> value;
    return value;
}

std::string StreamBuffer::readString(size_t len) {

    std::string str(len, '\0');
    sgetn(str.data(), len);
    return str;
}

} // geck