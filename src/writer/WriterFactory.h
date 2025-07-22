#pragma once

#include <memory>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

#include "FileWriter.h"
#include "WriterExceptions.h"

namespace geck {

// Forward declarations of format classes
class Map;
class Pro;
class Frm;
class Msg; 
class Lst;
class Pal;
class Gam;
class Dat;

// Forward declarations of writer classes  
class MapWriter;
class ProWriter;
class FrmWriter;
class MsgWriter;
class LstWriter;
class PalWriter;
class GamWriter;
class DatWriter;

/**
 * Factory class for creating file format writers with consistent interface.
 * Provides automatic format detection and unified writer creation.
 */
class WriterFactory {
public:
    enum class Format {
        UNKNOWN = 0,
        MAP = 1,
        PRO = 2, 
        FRM = 3,
        MSG = 4,
        LST = 5,
        PAL = 6,
        GAM = 7,
        DAT = 8
    };

    struct FormatInfo {
        Format format;
        std::string name;
        std::vector<std::string> extensions;
        std::string description;
        size_t typical_file_size = 0;
        bool supports_compression = false;
    };

    // Template-based writer creation
    template<typename T>
    static std::unique_ptr<FileWriter<T>> createWriter(Format format);

    // Format detection from file extension
    static Format detectFormat(const std::filesystem::path& filePath);
    
    // Format detection from extension string
    static Format detectFormat(const std::string& extension);

    // Utility methods
    static bool isFormatSupported(Format format);
    static bool isFormatSupported(const std::string& extension);
    static std::vector<std::string> getSupportedExtensions();
    static FormatInfo getFormatInfo(Format format);
    static std::vector<FormatInfo> getAllSupportedFormats();

    // File organization helpers
    static std::unordered_map<Format, std::vector<std::filesystem::path>> 
        groupFilesByFormat(const std::vector<std::filesystem::path>& files);

    // Create writer with additional configuration
    template<typename T>
    static std::unique_ptr<FileWriter<T>> createWriterWithCallback(
        Format format, std::function<void*(uint32_t)> callback);

    // Generic writer creation (returns base class pointer)
    static std::unique_ptr<void> createGenericWriter(Format format);

private:
    static const std::unordered_map<std::string, Format> _extensionMap;
    static const std::unordered_map<Format, FormatInfo> _formatInfoMap;
    
    // Template specializations for writer creation
    template<typename T>
    static std::unique_ptr<FileWriter<T>> createWriterImpl(Format format);
};

// Template specializations declaration
template<>
std::unique_ptr<FileWriter<Map::MapFile>> WriterFactory::createWriter<Map::MapFile>(Format format);

template<>
std::unique_ptr<FileWriter<Pro>> WriterFactory::createWriter<Pro>(Format format);

template<>
std::unique_ptr<FileWriter<Frm>> WriterFactory::createWriter<Frm>(Format format);

template<>
std::unique_ptr<FileWriter<Msg>> WriterFactory::createWriter<Msg>(Format format);

template<>
std::unique_ptr<FileWriter<Lst>> WriterFactory::createWriter<Lst>(Format format);

template<>
std::unique_ptr<FileWriter<Pal>> WriterFactory::createWriter<Pal>(Format format);

template<>
std::unique_ptr<FileWriter<Gam>> WriterFactory::createWriter<Gam>(Format format);

// Helper macro for format validation
#define VALIDATE_WRITER_FORMAT(expected_format, actual_format, type_name) \
    do { \
        if (actual_format != expected_format) { \
            throw FormatWriteException( \
                "Format mismatch: expected " #expected_format " for " type_name " writer, got " + \
                std::to_string(static_cast<int>(actual_format)), ""); \
        } \
    } while(0)

} // namespace geck