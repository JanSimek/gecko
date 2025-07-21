#include "StreamBuffer.h"

#include <zlib.h>
#include <cstring>
#include <algorithm>

namespace geck {

StreamBuffer::StreamBuffer(std::vector<uint8_t> data, ENDIANNESS endianness)
    : _endianness(endianness) {
    _buffer.resize(data.size());
    auto cBuf = _buffer.data();
    memcpy(cBuf, data.data(), static_cast<size_t>(data.size()));
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

StreamBuffer& StreamBuffer::skipBytes(size_t numberOfBytes) {
    auto cBuf = _buffer.data();
    setg(cBuf, gptr() + numberOfBytes, cBuf + _buffer.size());
    return *this;
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

StreamBuffer& StreamBuffer::operator>>(int32_t& value) {
    return *this >> (uint32_t&)value;
}

StreamBuffer& StreamBuffer::operator>>(uint16_t& value) {
    char* buff = reinterpret_cast<char*>(&value);
    sgetn(buff, sizeof(value));
    if (endianness() == ENDIANNESS::BIG) {
        std::reverse(buff, buff + sizeof(value));
    }
    return *this;
}

StreamBuffer& StreamBuffer::operator>>(int16_t& value) {
    return *this >> (uint16_t&)value;
}

StreamBuffer& StreamBuffer::operator>>(uint8_t& value) {
    sgetn(reinterpret_cast<char*>(&value), sizeof(value));
    return *this;
}

StreamBuffer& StreamBuffer::operator>>(int8_t& value) {
    return *this >> (uint8_t&)value;
}

StreamBuffer::ENDIANNESS StreamBuffer::endianness() {
    return _endianness;
}

void StreamBuffer::setEndianness(ENDIANNESS value) {
    _endianness = value;
}

size_t StreamBuffer::bytesRemains() {
    return size() - position();
}

uint32_t StreamBuffer::uint32() {
    uint32_t value = 0;
    *this >> value;
    return value;
}

int32_t StreamBuffer::int32() {
    return uint32();
}

uint16_t StreamBuffer::uint16() {
    uint16_t value = 0;
    *this >> value;
    return value;
}

int16_t StreamBuffer::int16() {
    return uint16();
}

uint8_t StreamBuffer::uint8() {
    uint8_t value = 0;
    *this >> value;
    return value;
}

int8_t StreamBuffer::int8() {
    return uint8();
}

std::string StreamBuffer::readString(size_t len) {

    std::string str(len, '\0'); // construct string to stream size
    sgetn(str.data(), len);
    return str;
}

} // geck