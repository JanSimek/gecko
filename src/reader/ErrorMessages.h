#pragma once

#include <string>
#include <filesystem>

namespace geck {

class ErrorMessages {
public:
    // File format specific error templates
    static std::string invalidFileSize(const std::filesystem::path& path, size_t actual, size_t expected) {
        return formatError("INVALID_FILE_SIZE", path,
            "File size mismatch - expected " + std::to_string(expected) + " bytes, got " + std::to_string(actual));
    }

    static std::string corruptedData(const std::filesystem::path& path, const std::string& details) {
        return formatError("CORRUPTED_DATA", path, "Data corruption detected: " + details);
    }

    static std::string invalidStringLength(const std::filesystem::path& path, size_t length) {
        return formatError("INVALID_STRING_LENGTH", path,
            "String length " + std::to_string(length) + " is invalid (too long or zero)");
    }

    // Format-specific templates
    static std::string datFileError(const std::filesystem::path& path, const std::string& details) {
        return formatError("DAT_FORMAT_ERROR", path, "DAT archive error: " + details);
    }

    static std::string frmFileError(const std::filesystem::path& path, const std::string& details) {
        return formatError("FRM_FORMAT_ERROR", path, "FRM animation error: " + details);
    }

private:
    static std::string formatError(const std::string& errorCode, const std::filesystem::path& path,
        const std::string& details) {
        return "[" + errorCode + "] " + path.filename().string() + ": " + details;
    }
};

} // namespace geck
