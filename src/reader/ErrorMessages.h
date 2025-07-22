#pragma once

#include <string>
#include <filesystem>
#include <sstream>
#include <cstdint>

namespace geck {

class ErrorMessages {
public:
    // File format specific error templates
    static std::string invalidFileSize(const std::filesystem::path& path, size_t actual, size_t expected) {
        return formatError("INVALID_FILE_SIZE", path, 
            "File size mismatch - expected " + std::to_string(expected) + " bytes, got " + std::to_string(actual));
    }
    
    static std::string invalidMagicNumber(const std::filesystem::path& path, uint32_t actual, uint32_t expected) {
        std::ostringstream oss;
        oss << "Invalid magic number - expected 0x" << std::hex << expected 
            << ", got 0x" << std::hex << actual;
        return formatError("INVALID_MAGIC", path, oss.str());
    }
    
    static std::string unsupportedVersion(const std::filesystem::path& path, uint32_t version, const std::string& formatName) {
        return formatError("UNSUPPORTED_VERSION", path, 
            formatName + " version " + std::to_string(version) + " is not supported");
    }
    
    static std::string corruptedData(const std::filesystem::path& path, const std::string& details) {
        return formatError("CORRUPTED_DATA", path, "Data corruption detected: " + details);
    }
    
    static std::string unexpectedEndOfFile(const std::filesystem::path& path, size_t position) {
        return formatError("UNEXPECTED_EOF", path, 
            "Unexpected end of file at position " + std::to_string(position));
    }
    
    // Field validation errors
    static std::string invalidFieldValue(const std::filesystem::path& path, const std::string& fieldName, 
                                       const std::string& actual, const std::string& expected) {
        return formatError("INVALID_FIELD", path, 
            "Field '" + fieldName + "' has invalid value '" + actual + "' (expected " + expected + ")");
    }
    
    static std::string invalidArraySize(const std::filesystem::path& path, const std::string& arrayName, 
                                      size_t actual, size_t maxExpected) {
        return formatError("INVALID_ARRAY_SIZE", path, 
            "Array '" + arrayName + "' size " + std::to_string(actual) + 
            " exceeds maximum " + std::to_string(maxExpected));
    }
    
    static std::string invalidStringLength(const std::filesystem::path& path, size_t length) {
        return formatError("INVALID_STRING_LENGTH", path, 
            "String length " + std::to_string(length) + " is invalid (too long or zero)");
    }
    
    // Format-specific templates
    static std::string datFileError(const std::filesystem::path& path, const std::string& details) {
        return formatError("DAT_FORMAT_ERROR", path, "DAT archive error: " + details);
    }
    
    static std::string proFileError(const std::filesystem::path& path, const std::string& details) {
        return formatError("PRO_FORMAT_ERROR", path, "PRO object error: " + details);
    }
    
    static std::string frmFileError(const std::filesystem::path& path, const std::string& details) {
        return formatError("FRM_FORMAT_ERROR", path, "FRM animation error: " + details);
    }
    
    static std::string mapFileError(const std::filesystem::path& path, const std::string& details) {
        return formatError("MAP_FORMAT_ERROR", path, "MAP file error: " + details);
    }
    
    // I/O specific errors
    static std::string fileOpenError(const std::filesystem::path& path, const std::string& reason = "") {
        std::string details = "Could not open file";
        if (!reason.empty()) {
            details += ": " + reason;
        }
        return formatError("FILE_OPEN_ERROR", path, details);
    }
    
    static std::string fileReadError(const std::filesystem::path& path, size_t position) {
        return formatError("FILE_READ_ERROR", path, 
            "Read error at position " + std::to_string(position));
    }
    
    // Warning templates
    static std::string unusualFieldValue(const std::filesystem::path& path, const std::string& fieldName, 
                                       const std::string& value) {
        return formatWarning("UNUSUAL_FIELD_VALUE", path, 
            "Field '" + fieldName + "' has unusual value: " + value);
    }
    
    static std::string deprecatedFeature(const std::filesystem::path& path, const std::string& feature) {
        return formatWarning("DEPRECATED_FEATURE", path, 
            "File uses deprecated feature: " + feature);
    }

private:
    static std::string formatError(const std::string& errorCode, const std::filesystem::path& path, 
                                 const std::string& details) {
        return "[" + errorCode + "] " + path.filename().string() + ": " + details;
    }
    
    static std::string formatWarning(const std::string& warningCode, const std::filesystem::path& path, 
                                   const std::string& details) {
        return "[WARNING:" + warningCode + "] " + path.filename().string() + ": " + details;
    }
};

// Convenience macros for common error patterns
#define READER_ERROR(path, message) \
    geck::ErrorMessages::formatError("READER_ERROR", path, message)

#define FORMAT_ERROR(formatType, path, message) \
    geck::ErrorMessages::formatType##FileError(path, message)

} // namespace geck