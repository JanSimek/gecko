#pragma once

#include <fstream>
#include <filesystem>

// htonl, htons
#ifdef _WIN32
#define NOMINMAX                   // sfml conflict
#pragma comment(lib, "ws2_32.lib") // Winsock
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

template <typename T>
class FileWriter {
protected:
    std::ofstream stream;
    std::filesystem::path path;

public:
    virtual ~FileWriter() = default;
    void openFile(const std::filesystem::path& filePath) {

        stream = std::ofstream{ filePath.string(), std::ofstream::out | std::ofstream::binary };
        this->path = filePath;

        if (!stream.is_open()) {
            throw std::runtime_error{ "Could not open file for writing " + path.string() };
        }
    }

    virtual bool write(const T& object) = 0;

    inline void write_be_32(uint32_t value) {
        uint32_t be_value = htonl(value); // convert to big endian
        stream.write(reinterpret_cast<const char*>(&be_value), sizeof(be_value));
        //    stream.put(value >> 24);
        //    stream.put(value >> 16);
        //    stream.put(value >> 8);
        //    stream.put(value);
    }

    inline void write_be_16(uint16_t value) {
        uint16_t be_value = htons(value); // convert to big endian
        stream.write(reinterpret_cast<const char*>(&be_value), sizeof(be_value));
    }
};
