#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <filesystem>
#include <spdlog/spdlog.h>

// Network byte order functions
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // sfml conflict
#endif
#pragma comment(lib, "ws2_32.lib") // Winsock
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

#include "WriterExceptions.h"

namespace geck {

/**
 * @brief Safe binary writing with bounds checking and error handling.
 *
 * Provides consistent endianness conversion and validation for all binary write operations.
 */
class BinaryWriteUtils {
private:
    std::ofstream& _stream;
    std::filesystem::path _filePath;
    size_t _bytesWritten = 0;
    size_t _position = 0;

    void checkStreamState() const {
        if (!_stream.is_open()) {
            throw WriteException("Stream is not open", _filePath);
        }
        if (_stream.fail()) {
            throw WriteException("Stream is in failed state", _filePath);
        }
        if (_stream.bad()) {
            throw WriteException("Stream encountered a fatal error", _filePath);
        }
    }

    void updatePosition(size_t bytes) {
        _bytesWritten += bytes;
        _position += bytes;
    }

public:
    explicit BinaryWriteUtils(std::ofstream& stream, const std::filesystem::path& filePath = "")
        : _stream(stream)
        , _filePath(filePath) { }

    // Endianness conversion functions
    static uint16_t hostToBE16(uint16_t value) {
        return htons(value);
    }

    static uint32_t hostToBE32(uint32_t value) {
        return htonl(value);
    }

    // Basic write operations with error checking
    void writeU8(uint8_t value) {
        checkStreamState();
        _stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
        if (!_stream.good()) {
            throw WriteException("Failed to write uint8_t", _filePath);
        }
        updatePosition(sizeof(value));
        spdlog::trace("Wrote uint8_t: {} at position {}", value, _position - sizeof(value));
    }

    // Big-endian write operations (Fallout format)
    void writeBE16(uint16_t value) {
        checkStreamState();
        uint16_t beValue = hostToBE16(value);
        _stream.write(reinterpret_cast<const char*>(&beValue), sizeof(beValue));
        if (!_stream.good()) {
            throw WriteException("Failed to write big-endian uint16_t", _filePath);
        }
        updatePosition(sizeof(beValue));
        spdlog::trace("Wrote BE uint16_t: {} at position {}", value, _position - sizeof(beValue));
    }

    void writeBE32(uint32_t value) {
        checkStreamState();
        uint32_t beValue = hostToBE32(value);
        _stream.write(reinterpret_cast<const char*>(&beValue), sizeof(beValue));
        if (!_stream.good()) {
            throw WriteException("Failed to write big-endian uint32_t", _filePath);
        }
        updatePosition(sizeof(beValue));
        spdlog::trace("Wrote BE uint32_t: {} at position {}", value, _position - sizeof(beValue));
    }

    void writeBE32Signed(int32_t value) {
        uint32_t beValue = hostToBE32(static_cast<uint32_t>(value));
        _stream.write(reinterpret_cast<const char*>(&beValue), sizeof(beValue));
        if (!_stream.good()) {
            throw WriteException("Failed to write big-endian int32_t", _filePath);
        }
        updatePosition(sizeof(beValue));
        spdlog::trace("Wrote BE int32_t: {} at position {}", value, _position - sizeof(beValue));
    }

    // String operations
    void writeFixedString(const std::string& str, size_t length) {
        checkStreamState();
        if (str.length() > length) {
            throw ValidationException("String too long for fixed field", _filePath, "string");
        }

        _stream.write(str.c_str(), str.length());
        if (!_stream.good()) {
            throw WriteException("Failed to write string data", _filePath);
        }

        // Pad with null bytes
        size_t padding = length - str.length();
        for (size_t i = 0; i < padding; ++i) {
            _stream.write("\0", 1);
        }
        if (!_stream.good()) {
            throw WriteException("Failed to write string padding", _filePath);
        }

        updatePosition(length);
        spdlog::trace("Wrote fixed string: '{}' ({} bytes) at position {}", str, length, _position - length);
    }

    // Padding and alignment
    void writePadding(size_t bytes, uint8_t value = 0x00) {
        checkStreamState();
        for (size_t i = 0; i < bytes; ++i) {
            _stream.write(reinterpret_cast<const char*>(&value), 1);
        }
        if (!_stream.good()) {
            throw WriteException("Failed to write padding", _filePath);
        }
        updatePosition(bytes);
        spdlog::trace("Wrote {} bytes of padding (value: 0x{:02X}) at position {}", bytes, value, _position - bytes);
    }

    // Position and status information
    size_t getPosition() const {
        return _position;
    }

    size_t getBytesWritten() const {
        return _bytesWritten;
    }

    void flush() {
        checkStreamState();
        _stream.flush();
        if (!_stream.good()) {
            throw WriteException("Failed to flush stream", _filePath);
        }
    }

    // Logging with description
    void writeWithLog(uint32_t value, const std::string& description) {
        writeBE32(value);
        spdlog::trace("Wrote {}: {} at position {}", description, value, _position - sizeof(value));
    }

    void writeWithLog(uint16_t value, const std::string& description) {
        writeBE16(value);
        spdlog::trace("Wrote {}: {} at position {}", description, value, _position - sizeof(value));
    }

    void writeWithLog(int32_t value, const std::string& description) {
        writeBE32Signed(value);
        spdlog::trace("Wrote {}: {} at position {}", description, value, _position - sizeof(value));
    }
};

} // namespace geck