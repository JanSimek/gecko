#pragma once

#include <vector>
#include <string>
#include <array>
#include <map>
#include <cstdint>
#include <spdlog/spdlog.h>

#include "StreamBuffer.h"
#include "ReaderExceptions.h"
#include "ReaderDiagnostics.h"

namespace geck {

class BinaryUtils {
public:
    explicit BinaryUtils(StreamBuffer& stream, const std::filesystem::path& filePath)
        : _stream(stream), _filePath(filePath) {}

    template<typename T>
    std::vector<T> readArray(size_t count) {
        std::vector<T> result;
        result.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            result.push_back(readValue<T>());
        }
        
        spdlog::trace("Read array of {} elements (type size: {})", count, sizeof(T));
        return result;
    }

    template<size_t N>
    std::array<uint32_t, N> readFixedArray() {
        std::array<uint32_t, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = readBE32();
        }
        spdlog::trace("Read fixed array of {} uint32 elements", N);
        return result;
    }

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

    std::string readNullTerminatedString() {
        std::string result;
        char c;
        
        while (true) {
            validatePosition(1);
            c = static_cast<char>(_stream.uint8());
            
            if (c == '\0') {
                break;
            }
            result += c;
        }
        
        spdlog::trace("Read null-terminated string: '{}'", result);
        return result;
    }

    void skipWithLog(size_t bytes, const std::string& description) {
        validatePosition(bytes);
        
        for (size_t i = 0; i < bytes; ++i) {
            _stream.uint8();
        }
        
        ReaderDiagnostics::trackRead(bytes, "skip: " + description);
        spdlog::debug("Skipped {} bytes: {}", bytes, description);
    }

    uint32_t readBE32() {
        validatePosition(4);
        ReaderDiagnostics::trackRead(4);
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

    template<typename T>
    void skipArray(size_t count, const std::string& description = "") {
        size_t totalBytes = count * sizeof(T);
        skipWithLog(totalBytes, description.empty() ? 
            "Array of " + std::to_string(count) + " elements" : description);
    }

    struct Position {
        size_t current;
        size_t total;
        double percentage() const { return (double)current / total * 100.0; }
    };

    Position getPosition() const {
        return {_stream.position(), _stream.size()};
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
            throw ParseException("Attempt to read beyond end of file", 
                                _filePath, _stream.position());
        }
    }

    template<typename T>
    T readValue() {
        if constexpr (sizeof(T) == 1) {
            return static_cast<T>(readU8());
        } else if constexpr (sizeof(T) == 2) {
            return static_cast<T>(readBE16());
        } else if constexpr (sizeof(T) == 4) {
            return static_cast<T>(readBE32());
        } else {
            static_assert(sizeof(T) <= 4, "Unsupported type size for readValue");
        }
    }
};

// Helper class for structured field reading with optional preservation
template<typename T>
class StructuredReader {
private:
    BinaryUtils& _utils;
    T* _object;
    bool _preserveAll;
    std::map<std::string, uint32_t> _preservedFields;

public:
    StructuredReader(BinaryUtils& utils, T* object, bool preserveAll = false)
        : _utils(utils), _object(object), _preserveAll(preserveAll) {}

    template<typename FieldType>
    StructuredReader& readField(FieldType T::*field, const std::string& name = "") {
        auto value = _utils.readValue<FieldType>();
        _object->*field = value;
        
        if (!name.empty()) {
            spdlog::trace("Read field '{}': {}", name, value);
        }
        return *this;
    }

    StructuredReader& skipField(size_t bytes, const std::string& name) {
        if (_preserveAll) {
            auto value = (bytes == 4) ? _utils.readBE32() : _utils.readU8();
            _preservedFields[name] = value;
            spdlog::trace("Preserved field '{}': {}", name, value);
        } else {
            _utils.skipWithLog(bytes, name);
        }
        return *this;
    }

    const std::map<std::string, uint32_t>& getPreservedFields() const {
        return _preservedFields;
    }
};

} // namespace geck