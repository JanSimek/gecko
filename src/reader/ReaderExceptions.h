#pragma once

#include <stdexcept>
#include <string>
#include <filesystem>

namespace geck {

class FileReaderException : public std::runtime_error {
public:
    explicit FileReaderException(const std::string& message)
        : std::runtime_error(message) {}

    explicit FileReaderException(const std::string& message, const std::filesystem::path& filePath)
        : std::runtime_error(message + " (file: " + filePath.string() + ")") {}
};

class ParseException : public FileReaderException {
public:
    explicit ParseException(const std::string& message)
        : FileReaderException("Parse error: " + message) {}

    explicit ParseException(const std::string& message, const std::filesystem::path& filePath)
        : FileReaderException("Parse error: " + message, filePath) {}

    explicit ParseException(const std::string& message, const std::filesystem::path& filePath, size_t position)
        : FileReaderException("Parse error: " + message + " at position " + std::to_string(position), filePath) {}
};

class IOException : public FileReaderException {
public:
    explicit IOException(const std::string& message)
        : FileReaderException("IO error: " + message) {}

    explicit IOException(const std::string& message, const std::filesystem::path& filePath)
        : FileReaderException("IO error: " + message, filePath) {}
};

class UnsupportedFormatException : public FileReaderException {
public:
    explicit UnsupportedFormatException(const std::string& message)
        : FileReaderException("Unsupported format: " + message) {}

    explicit UnsupportedFormatException(const std::string& message, const std::filesystem::path& filePath)
        : FileReaderException("Unsupported format: " + message, filePath) {}
};

} // namespace geck