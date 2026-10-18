#pragma once

#include <string>
#include <cstdint>
#include <spdlog/spdlog.h>

#include "StreamBuffer.h"
#include "ReaderExceptions.h"

namespace geck {

class BinaryUtils {
public:
    explicit BinaryUtils(StreamBuffer& stream, const std::filesystem::path& filePath)
        : _stream(stream)
        , _filePath(filePath) { }

    std::string readFixedString(size_t length) {
        validatePosition(length);
        std::string result = _stream.readString(length);

        // Remove null terminators and trim
        auto nullPos = result.find('\0');
        if (nullPos != std::string::npos) {
            result = result.substr(0, nullPos);
        }

        spdlog::trace("Read fixed string of length {}: '{}'", length, result);
        return result;
    }

    uint32_t readBE32() {
        validatePosition(4);
        return _stream.uint32();
    }

    uint16_t readBE16() {
        validatePosition(2);
        return _stream.uint16();
    }

    uint8_t readU8() {
        validatePosition(1);
        return _stream.uint8();
    }

    int32_t readBE32Signed() {
        return static_cast<int32_t>(readBE32());
    }

    struct Position {
        size_t current;
        size_t total;
        double percentage() const { return (double)current / total * 100.0; }
    };

    Position getPosition() const {
        return { _stream.position(), _stream.size() };
    }

    size_t bytesRemaining() const {
        return _stream.size() - _stream.position();
    }

    void setPosition(size_t pos) {
        if (pos > _stream.size()) {
            throw ParseException("Invalid seek position", _filePath, pos);
        }
        _stream.setPosition(pos);
        spdlog::trace("Seeking to position {}", pos);
    }

private:
    StreamBuffer& _stream;
    std::filesystem::path _filePath;

    void validatePosition(size_t requiredBytes) {
        if (_stream.position() + requiredBytes > _stream.size()) {
            throw ParseException("Attempt to read beyond end of file", _filePath, _stream.position());
        }
    }
};

} // namespace geck