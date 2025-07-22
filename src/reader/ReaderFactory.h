#pragma once

#include <memory>
#include <string>
#include <filesystem>
#include <vector>
#include <map>

#include "format/IFile.h"
#include "FileParser.h"
#include "ReaderExceptions.h"

namespace geck {

// Forward declarations
class Dat;
class Pro; 
class Frm;
class Pal;
class Gam;
class Msg;
class Lst;
class Map;

class ReaderFactory {
public:
    enum class Format {
        DAT,
        PRO,
        FRM,
        PAL,
        GAM,
        MSG,
        LST,
        MAP,
        AUTO_DETECT,
        UNKNOWN
    };
    
    struct FormatInfo {
        Format format;
        std::string name;
        std::vector<std::string> extensions;
        std::vector<uint8_t> magic_signature;
        size_t signature_offset;
        size_t min_file_size;
        double confidence; // 0.0 - 1.0, for auto-detection
    };
    
    // Factory methods for creating readers
    template<typename T>
    static std::unique_ptr<FileParser<T>> createReader(Format format);
    
    // Auto-detection based factory methods (returns void* - caller must cast)
    static void* createGenericReader(const std::filesystem::path& filePath, Format& detectedFormat);
    static void* createGenericReader(const std::vector<uint8_t>& data, const std::string& filename, Format& detectedFormat);
    
    // Format detection utilities
    static Format detectFormat(const std::filesystem::path& filePath);
    static Format detectFormat(const std::vector<uint8_t>& data, const std::string& filename = "");
    static FormatInfo getFormatInfo(Format format);
    static std::vector<FormatInfo> getAllSupportedFormats();
    
    // Validation utilities
    static bool isFormatSupported(Format format);
    static bool isFormatSupported(const std::string& extension);
    static std::vector<std::string> getSupportedExtensions();
    
    // Reading convenience methods
    template<typename T>
    static std::unique_ptr<T> readFile(const std::filesystem::path& filePath);
    
    template<typename T>
    static std::unique_ptr<T> readFile(const std::vector<uint8_t>& data, const std::string& filename);
    
    // Batch processing utilities
    template<typename T>
    static std::vector<std::unique_ptr<T>> readFiles(const std::vector<std::filesystem::path>& filePaths);
    
    static std::map<Format, std::vector<std::filesystem::path>> groupFilesByFormat(
        const std::vector<std::filesystem::path>& filePaths);
    
private:
    static Format detectByExtension(const std::filesystem::path& filePath);
    static Format detectByMagicNumber(const std::vector<uint8_t>& data);
    static Format detectByContent(const std::vector<uint8_t>& data);
    static double calculateConfidence(Format format, const std::vector<uint8_t>& data);
    
    static const std::map<Format, FormatInfo> format_info_map;
    static const std::map<std::string, Format> extension_format_map;
};

// Template implementations

template<typename T>
inline std::unique_ptr<FileParser<T>> ReaderFactory::createReader(Format format) {
    (void)format; // Suppress unused parameter warning
    // This will be specialized for each type
    throw UnsupportedFormatException("Unsupported format for template type");
}

template<typename T>
inline std::unique_ptr<T> ReaderFactory::readFile(const std::filesystem::path& filePath) {
    auto reader = createReader<T>(detectFormat(filePath));
    return reader->openFile(filePath);
}

template<typename T>
inline std::unique_ptr<T> ReaderFactory::readFile(const std::vector<uint8_t>& data, const std::string& filename) {
    auto reader = createReader<T>(detectFormat(data, filename));
    return reader->openFile(filename, data);
}

template<typename T>
inline std::vector<std::unique_ptr<T>> ReaderFactory::readFiles(const std::vector<std::filesystem::path>& filePaths) {
    std::vector<std::unique_ptr<T>> results;
    results.reserve(filePaths.size());
    
    for (const auto& filePath : filePaths) {
        try {
            results.push_back(readFile<T>(filePath));
        } catch (const std::exception& e) {
            // Log error and continue with next file
            spdlog::error("Failed to read file {}: {}", filePath.string(), e.what());
            results.push_back(nullptr);
        }
    }
    
    return results;
}

} // namespace geck