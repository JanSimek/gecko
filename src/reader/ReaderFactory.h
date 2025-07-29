#pragma once

#include <memory>
#include <string>
#include <filesystem>
#include <vector>
#include <map>
#include <any>
#include <fstream>
#include <ranges>
#include <algorithm>

#include "format/IFile.h"
#include "FileParser.h"
#include "ReaderExceptions.h"
#include "../util/Factory.h"

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

/**
 * @brief Modern ReaderFactory with template-based interface and utility methods
 * 
 * Provides both simple file reading and advanced format detection/utility methods.
 */
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

    // ========================================
    // Main Reading Interface (Template-based)
    // ========================================
    
    /**
     * @brief Read file from filesystem path
     * @tparam T The expected return type (Dat, Pro, Frm, etc.)
     * @param filepath Path to file
     * @return Created reader instance
     */
    template<typename T>
    [[nodiscard]] static std::unique_ptr<T> readFile(const std::filesystem::path& filepath) {
        try {
            auto format = detectFormat(filepath);
            auto reader = createReader<T>(format);
            if (!reader) {
                throw FileReaderException("Failed to create reader for " + filepath.filename().string());
            }
            return reader->openFile(filepath);
        } catch (const std::exception& e) {
            throw IOException("ReaderFactory: Failed to read " + filepath.filename().string() + ": " + e.what(), filepath);
        }
    }
    
    /**
     * @brief Read file from memory data (for VFS support)
     * @tparam T The expected return type
     * @param data File data loaded from VFS
     * @param filename Original filename for error reporting and format detection
     * @return Created reader instance
     */
    template<typename T>
    [[nodiscard]] static std::unique_ptr<T> readFileFromMemory(const std::vector<uint8_t>& data, const std::string& filename) {
        try {
            auto format = detectFormat(data, filename);
            auto reader = createReader<T>(format);
            if (!reader) {
                throw FileReaderException("Failed to create reader for " + filename);
            }
            return reader->openFile(filename, data);
        } catch (const std::exception& e) {
            throw IOException("ReaderFactory: Failed to read " + filename + " from memory: " + e.what(), filename);
        }
    }

    /**
     * @brief Read multiple files of the same type
     * @tparam T The expected return type
     * @param filePaths Vector of file paths
     * @return Vector of parsed objects
     */
    template<typename T>
    [[nodiscard]] static std::vector<std::unique_ptr<T>> readFiles(const std::vector<std::filesystem::path>& filePaths) {
        std::vector<std::unique_ptr<T>> results;
        results.reserve(filePaths.size());
        
        for (const auto& path : filePaths) {
            results.emplace_back(readFile<T>(path));
        }
        
        return results;
    }

    // ========================================
    // Reader Creation (Lower-level interface)
    // ========================================
    
    template<typename T>
    [[nodiscard]] static std::unique_ptr<FileParser<T>> createReader(Format format);

    // ========================================
    // Format Detection and Utilities
    // ========================================
    
    [[nodiscard]] static Format detectFormat(const std::filesystem::path& filePath);
    [[nodiscard]] static Format detectFormat(const std::vector<uint8_t>& data, const std::string& filename);
    [[nodiscard]] static Format detectByExtension(const std::filesystem::path& filePath);
    [[nodiscard]] static Format detectByMagicNumber(const std::vector<uint8_t>& data);
    [[nodiscard]] static Format detectByContent(const std::vector<uint8_t>& data);
    
    [[nodiscard]] static double calculateConfidence(Format format, const std::vector<uint8_t>& data);
    [[nodiscard]] static FormatInfo getFormatInfo(Format format);
    [[nodiscard]] static std::vector<FormatInfo> getAllSupportedFormats();
    
    [[nodiscard]] static bool isFormatSupported(Format format);
    [[nodiscard]] static bool isFormatSupported(const std::string& extension);
    [[nodiscard]] static std::vector<std::string> getSupportedExtensions();
    
    [[nodiscard]] static std::map<Format, std::vector<std::filesystem::path>> 
    groupFilesByFormat(const std::vector<std::filesystem::path>& filePaths);

private:
    // Generic reader creation (legacy interface support)
    static void* createGenericReader(const std::filesystem::path& filePath, Format& detectedFormat);
    static void* createGenericReader(const std::vector<uint8_t>& data, const std::string& filename, Format& detectedFormat);
    
    // Format info maps
    static const std::map<Format, FormatInfo> format_info_map;
    static const std::map<std::string, Format> extension_format_map;
};

// Template specializations (declared here, implemented in .cpp)
template<> std::unique_ptr<FileParser<Dat>> ReaderFactory::createReader<Dat>(Format format);
template<> std::unique_ptr<FileParser<Pro>> ReaderFactory::createReader<Pro>(Format format);
template<> std::unique_ptr<FileParser<Frm>> ReaderFactory::createReader<Frm>(Format format);
template<> std::unique_ptr<FileParser<Pal>> ReaderFactory::createReader<Pal>(Format format);
template<> std::unique_ptr<FileParser<Gam>> ReaderFactory::createReader<Gam>(Format format);
template<> std::unique_ptr<FileParser<Msg>> ReaderFactory::createReader<Msg>(Format format);
template<> std::unique_ptr<FileParser<Lst>> ReaderFactory::createReader<Lst>(Format format);
// Note: Map reader is not supported via createReader due to callback requirement
// Use MapReader constructor directly with the required callback

// Template implementations for generic createReader
template<typename T>
inline std::unique_ptr<FileParser<T>> ReaderFactory::createReader(Format format) {
    static_assert(sizeof(T) == 0, "Unsupported type for ReaderFactory::createReader. Add explicit specialization.");
    return nullptr;
}

} // namespace geck