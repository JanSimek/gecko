#pragma once

#include <stdexcept>
#include <string>
#include <filesystem>

namespace geck {

/// Base exception class for all file writing operations.
class FileWriterException : public std::runtime_error {
private:
    std::filesystem::path _filePath;

public:
    explicit FileWriterException(const std::string& message, const std::filesystem::path& filePath = "")
        : std::runtime_error(message)
        , _filePath(filePath) { }

    const std::filesystem::path& filePath() const noexcept {
        return _filePath;
    }
};

/// Exception thrown when a write I/O operation fails.
class WriteException : public FileWriterException {
public:
    explicit WriteException(const std::string& message, const std::filesystem::path& filePath)
        : FileWriterException("Write error: " + message, filePath) { }
};

/// Exception thrown when data validation fails before writing.
class ValidationException : public FileWriterException {
private:
    std::string _field;

public:
    explicit ValidationException(const std::string& message, const std::filesystem::path& filePath, const std::string& field = "")
        : FileWriterException("Validation error: " + message, filePath)
        , _field(field) { }

    const std::string& field() const noexcept {
        return _field;
    }
};

/// Exception thrown when file permissions prevent writing.
class PermissionException : public FileWriterException {
public:
    explicit PermissionException(const std::string& message, const std::filesystem::path& filePath)
        : FileWriterException("Permission error: " + message, filePath) { }
};

/// Exception thrown when attempting to write corrupted or incomplete data structures.
class CorruptDataException : public FileWriterException {
public:
    explicit CorruptDataException(const std::string& message, const std::filesystem::path& filePath)
        : FileWriterException("Data corruption error: " + message, filePath) { }
};

} // namespace geck