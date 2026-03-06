#pragma once

#include <stdexcept>
#include <string>
#include <filesystem>

namespace geck {

/**
 * Base exception class for all file writing operations.
 */
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

/**
 * Exception thrown when a write I/O operation fails.
 */
class WriteException : public FileWriterException {
public:
    explicit WriteException(const std::string& message, const std::filesystem::path& filePath, size_t bytesWritten = 0)
        : FileWriterException("Write error: " + message, filePath)
        , _bytesWritten(bytesWritten) { }

    size_t bytesWritten() const noexcept {
        return _bytesWritten;
    }

private:
    size_t _bytesWritten;
};

/**
 * Exception thrown when attempting to write data in an invalid format.
 */
class FormatWriteException : public FileWriterException {
public:
    explicit FormatWriteException(const std::string& message, const std::filesystem::path& filePath)
        : FileWriterException("Format error: " + message, filePath) { }
};

/**
 * Exception thrown when data validation fails before writing.
 */
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

/**
 * Exception thrown when insufficient disk space or file system limits are encountered.
 */
class DiskSpaceException : public FileWriterException {
private:
    size_t _bytesRequested;
    size_t _bytesAvailable;

public:
    explicit DiskSpaceException(const std::string& message, const std::filesystem::path& filePath,
        size_t bytesRequested = 0, size_t bytesAvailable = 0)
        : FileWriterException("Disk space error: " + message, filePath)
        , _bytesRequested(bytesRequested)
        , _bytesAvailable(bytesAvailable) { }

    size_t bytesRequested() const noexcept {
        return _bytesRequested;
    }

    size_t bytesAvailable() const noexcept {
        return _bytesAvailable;
    }
};

/**
 * Exception thrown when attempting to write to a file that already exists (when overwrite is not allowed).
 */
class FileExistsException : public FileWriterException {
public:
    explicit FileExistsException(const std::filesystem::path& filePath)
        : FileWriterException("File already exists", filePath) { }
};

/**
 * Exception thrown when file permissions prevent writing.
 */
class PermissionException : public FileWriterException {
public:
    explicit PermissionException(const std::string& message, const std::filesystem::path& filePath)
        : FileWriterException("Permission error: " + message, filePath) { }
};

/**
 * Exception thrown when attempting to write corrupted or incomplete data structures.
 */
class CorruptDataException : public FileWriterException {
private:
    std::string _dataType;

public:
    explicit CorruptDataException(const std::string& message, const std::filesystem::path& filePath, const std::string& dataType = "")
        : FileWriterException("Data corruption error: " + message, filePath)
        , _dataType(dataType) { }

    const std::string& dataType() const noexcept {
        return _dataType;
    }
};

} // namespace geck