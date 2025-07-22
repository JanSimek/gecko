#pragma once

#include <cstdint>
#include <fstream>
#include <vector>
#include <string>
#include <type_traits>
#include <filesystem>
#include <spdlog/spdlog.h>

// Network byte order functions
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // sfml conflict
#endif
#pragma comment(lib, "ws2_32.lib") // Winsock
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

#include "WriterExceptions.h"

namespace geck {

/**
 * Utility class for safe binary writing operations with bounds checking and error handling.
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
            throw WriteException("Stream is not open", _filePath, _bytesWritten);
        }
        if (_stream.fail()) {
            throw WriteException("Stream is in failed state", _filePath, _bytesWritten);
        }
        if (_stream.bad()) {
            throw WriteException("Stream encountered a fatal error", _filePath, _bytesWritten);
        }
    }

    void updatePosition(size_t bytes) {
        _bytesWritten += bytes;
        _position += bytes;
    }

public:
    explicit BinaryWriteUtils(std::ofstream& stream, const std::filesystem::path& filePath = "")
        : _stream(stream), _filePath(filePath) {}

    // Endianness conversion functions
    static uint16_t hostToBE16(uint16_t value) {
        return htons(value);
    }

    static uint32_t hostToBE32(uint32_t value) {
        return htonl(value);
    }

    static uint64_t hostToBE64(uint64_t value) {
        // Manual byte swap for 64-bit since htonll is not universally available
        return ((uint64_t)hostToBE32(value & 0xFFFFFFFF) << 32) | hostToBE32(value >> 32);
    }

    // Basic write operations with error checking
    void writeU8(uint8_t value) {
        checkStreamState();
        _stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
        if (!_stream.good()) {
            throw WriteException("Failed to write uint8_t", _filePath, _bytesWritten);
        }
        updatePosition(sizeof(value));
        spdlog::trace("Wrote uint8_t: {} at position {}", value, _position - sizeof(value));
    }

    void writeU16(uint16_t value) {
        checkStreamState();
        _stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
        if (!_stream.good()) {
            throw WriteException("Failed to write uint16_t", _filePath, _bytesWritten);
        }
        updatePosition(sizeof(value));
        spdlog::trace("Wrote uint16_t: {} at position {}", value, _position - sizeof(value));
    }

    void writeU32(uint32_t value) {
        checkStreamState();
        _stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
        if (!_stream.good()) {
            throw WriteException("Failed to write uint32_t", _filePath, _bytesWritten);
        }
        updatePosition(sizeof(value));
        spdlog::trace("Wrote uint32_t: {} at position {}", value, _position - sizeof(value));
    }

    void writeI32(int32_t value) {
        checkStreamState();
        _stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
        if (!_stream.good()) {
            throw WriteException("Failed to write int32_t", _filePath, _bytesWritten);
        }
        updatePosition(sizeof(value));
        spdlog::trace("Wrote int32_t: {} at position {}", value, _position - sizeof(value));
    }

    // Big-endian write operations (Fallout format)
    void writeBE16(uint16_t value) {
        uint16_t beValue = hostToBE16(value);
        writeU16(beValue);
    }

    void writeBE32(uint32_t value) {
        uint32_t beValue = hostToBE32(value);
        writeU32(beValue);
    }

    void writeBE32Signed(int32_t value) {
        uint32_t beValue = hostToBE32(static_cast<uint32_t>(value));
        _stream.write(reinterpret_cast<const char*>(&beValue), sizeof(beValue));
        if (!_stream.good()) {
            throw WriteException("Failed to write big-endian int32_t", _filePath, _bytesWritten);
        }
        updatePosition(sizeof(beValue));
        spdlog::trace("Wrote BE int32_t: {} at position {}", value, _position - sizeof(beValue));
    }

    void writeBE64(uint64_t value) {
        uint64_t beValue = hostToBE64(value);
        _stream.write(reinterpret_cast<const char*>(&beValue), sizeof(beValue));
        if (!_stream.good()) {
            throw WriteException("Failed to write big-endian uint64_t", _filePath, _bytesWritten);
        }
        updatePosition(sizeof(beValue));
        spdlog::trace("Wrote BE uint64_t: {} at position {}", value, _position - sizeof(beValue));
    }

    // String operations
    void writeFixedString(const std::string& str, size_t length) {
        checkStreamState();
        if (str.length() > length) {
            throw ValidationException("String too long for fixed field", _filePath, "string");
        }
        
        _stream.write(str.c_str(), str.length());
        if (!_stream.good()) {
            throw WriteException("Failed to write string data", _filePath, _bytesWritten);
        }
        
        // Pad with null bytes
        size_t padding = length - str.length();
        for (size_t i = 0; i < padding; ++i) {
            _stream.write("\0", 1);
        }
        if (!_stream.good()) {
            throw WriteException("Failed to write string padding", _filePath, _bytesWritten);
        }
        
        updatePosition(length);
        spdlog::trace("Wrote fixed string: '{}' ({} bytes) at position {}", str, length, _position - length);
    }

    void writeString(const std::string& str) {
        checkStreamState();
        _stream.write(str.c_str(), str.length());
        if (!_stream.good()) {
            throw WriteException("Failed to write string", _filePath, _bytesWritten);
        }
        updatePosition(str.length());
        spdlog::trace("Wrote string: '{}' ({} bytes) at position {}", str, str.length(), _position - str.length());
    }

    void writeCString(const std::string& str) {
        writeString(str);
        writeU8(0); // null terminator
    }

    // Array operations
    template<typename T>
    void writeArray(const std::vector<T>& data) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable for binary writing");
        
        checkStreamState();
        if (data.empty()) {
            spdlog::trace("Wrote empty array at position {}", _position);
            return;
        }
        
        _stream.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(T));
        if (!_stream.good()) {
            throw WriteException("Failed to write array data", _filePath, _bytesWritten);
        }
        
        size_t bytesToWrite = data.size() * sizeof(T);
        updatePosition(bytesToWrite);
        spdlog::trace("Wrote array: {} elements ({} bytes) at position {}", data.size(), bytesToWrite, _position - bytesToWrite);
    }

    template<typename T>
    void writeBEArray(const std::vector<T>& data) {
        for (const T& item : data) {
            if constexpr (sizeof(T) == 2) {
                writeBE16(static_cast<uint16_t>(item));
            } else if constexpr (sizeof(T) == 4) {
                writeBE32(static_cast<uint32_t>(item));
            } else if constexpr (sizeof(T) == 8) {
                writeBE64(static_cast<uint64_t>(item));
            } else {
                static_assert(sizeof(T) <= 8, "Unsupported type size for big-endian writing");
            }
        }
    }

    // Padding and alignment
    void writePadding(size_t bytes, uint8_t value = 0x00) {
        checkStreamState();
        for (size_t i = 0; i < bytes; ++i) {
            _stream.write(reinterpret_cast<const char*>(&value), 1);
        }
        if (!_stream.good()) {
            throw WriteException("Failed to write padding", _filePath, _bytesWritten);
        }
        updatePosition(bytes);
        spdlog::trace("Wrote {} bytes of padding (value: 0x{:02X}) at position {}", bytes, value, _position - bytes);
    }

    void alignTo(size_t alignment) {
        if (alignment == 0) return;
        
        size_t remainder = _position % alignment;
        if (remainder != 0) {
            size_t padding = alignment - remainder;
            writePadding(padding);
        }
    }

    // Raw data operations
    void writeRawData(const uint8_t* data, size_t size) {
        checkStreamState();
        if (data == nullptr && size > 0) {
            throw ValidationException("Cannot write null data", _filePath);
        }
        
        _stream.write(reinterpret_cast<const char*>(data), size);
        if (!_stream.good()) {
            throw WriteException("Failed to write raw data", _filePath, _bytesWritten);
        }
        updatePosition(size);
        spdlog::trace("Wrote {} bytes of raw data at position {}", size, _position - size);
    }

    void writeRawData(const std::vector<uint8_t>& data) {
        writeRawData(data.data(), data.size());
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
            throw WriteException("Failed to flush stream", _filePath, _bytesWritten);
        }
    }

    // Validation helpers
    void validateWriteCapacity(size_t bytesToWrite) const {
        // Check available disk space if possible
        try {
            auto space = std::filesystem::space(_filePath.parent_path());
            if (space.available < bytesToWrite) {
                throw DiskSpaceException("Insufficient disk space", _filePath, bytesToWrite, space.available);
            }
        } catch (const std::filesystem::filesystem_error&) {
            // Ignore filesystem errors for space checking
            spdlog::debug("Could not check disk space for {}", _filePath.string());
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