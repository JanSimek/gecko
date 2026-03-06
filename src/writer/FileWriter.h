#pragma once

#include <fstream>
#include <filesystem>
#include <memory>
#include <spdlog/spdlog.h>

#include "WriterExceptions.h"
#include "BinaryWriteUtils.h"

namespace geck {

template <typename T>
class FileWriter {
protected:
    std::ofstream _stream;
    std::filesystem::path _path;
    std::unique_ptr<BinaryWriteUtils> _utils;

public:
    virtual ~FileWriter() = default;

    void openFile(const std::filesystem::path& filePath, bool overwrite = true) {
        _path = filePath;

        // Check if file exists and overwrite is not allowed
        if (!overwrite && std::filesystem::exists(filePath)) {
            throw FileExistsException(filePath);
        }

        // Check parent directory exists and is writable
        auto parentPath = filePath.parent_path();
        if (!parentPath.empty() && !std::filesystem::exists(parentPath)) {
            try {
                std::filesystem::create_directories(parentPath);
            } catch (const std::filesystem::filesystem_error& e) {
                throw PermissionException("Cannot create parent directory: " + std::string(e.what()), filePath);
            }
        }

        // Open file for writing
        _stream = std::ofstream{ filePath.string(), std::ofstream::out | std::ofstream::binary };

        if (!_stream.is_open()) {
            throw WriteException("Could not open file for writing", filePath);
        }

        // Check if we can actually write to the file
        if (!_stream.good()) {
            throw PermissionException("File opened but cannot write", filePath);
        }

        // Initialize binary utilities
        _utils = std::make_unique<BinaryWriteUtils>(_stream, filePath);

        spdlog::debug("Opened file for writing: {}", filePath.string());
    }

    virtual bool write(const T& object) = 0;

    // Provide access to binary utilities for subclasses
    BinaryWriteUtils& getBinaryUtils() {
        if (!_utils) {
            throw WriteException("File not opened for writing", _path);
        }
        return *_utils;
    }

    const std::filesystem::path& getPath() const {
        return _path;
    }

    size_t getBytesWritten() const {
        return _utils ? _utils->getBytesWritten() : 0;
    }

    void flush() {
        if (_utils) {
            _utils->flush();
        }
    }

    bool isOpen() const {
        return _stream.is_open();
    }
};

} // namespace geck
