#include "ReaderFactory.h"

// Reader implementations
#include "dat/DatReader.h"
#include "pro/ProReader.h"
#include "frm/FrmReader.h"
#include "pal/PalReader.h"
#include "gam/GamReader.h"
#include "msg/MsgReader.h"
#include "lst/LstReader.h"
// #include "map/MapReader.h" - Not used, requires callback parameter

#include <fstream>
#include <algorithm>
#include <ranges>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace geck {

// Format information mapping
const std::map<ReaderFactory::Format, ReaderFactory::FormatInfo> ReaderFactory::format_info_map = {
    {Format::DAT, {Format::DAT, "Fallout Data Archive", {".dat"}, {}, 0, 32, 1.0}},
    {Format::PRO, {Format::PRO, "Fallout PRO Object", {".pro"}, {}, 0, 24, 1.0}},
    {Format::FRM, {Format::FRM, "Fallout Frame", {".frm"}, {}, 0, 62, 1.0}},  
    {Format::PAL, {Format::PAL, "Fallout Palette", {".pal"}, {}, 0, 768, 1.0}},
    {Format::GAM, {Format::GAM, "Fallout Game Save", {".gam"}, {'F', 'A', 'L', 'L'}, 0, 4, 1.0}},
    {Format::MSG, {Format::MSG, "Fallout Message File", {".msg"}, {}, 0, 1, 1.0}},
    {Format::LST, {Format::LST, "Fallout List File", {".lst"}, {}, 0, 1, 1.0}},
    {Format::MAP, {Format::MAP, "Fallout Map", {".map"}, {'M', 'A', 'P', ' '}, 0, 4, 1.0}},
};

// Extension to format mapping
const std::map<std::string, ReaderFactory::Format> ReaderFactory::extension_format_map = {
    {".dat", Format::DAT},
    {".pro", Format::PRO},
    {".frm", Format::FRM},
    {".pal", Format::PAL},
    {".gam", Format::GAM},
    {".msg", Format::MSG},
    {".lst", Format::LST},
    {".map", Format::MAP}
};

// Template specializations for createReader
template<>
std::unique_ptr<FileParser<Dat>> ReaderFactory::createReader<Dat>(Format format) {
    switch (format) {
    case Format::DAT:
        return std::make_unique<DatReader>();
    default:
        throw UnsupportedFormatException("Invalid format for Dat reader");
    }
}

template<>
std::unique_ptr<FileParser<Pro>> ReaderFactory::createReader<Pro>(Format format) {
    switch (format) {
    case Format::PRO:
        return std::make_unique<ProReader>();
    default:
        throw UnsupportedFormatException("Invalid format for Pro reader");
    }
}

template<>
std::unique_ptr<FileParser<Frm>> ReaderFactory::createReader<Frm>(Format format) {
    switch (format) {
    case Format::FRM:
        return std::make_unique<FrmReader>();
    default:
        throw UnsupportedFormatException("Invalid format for Frm reader");
    }
}

template<>
std::unique_ptr<FileParser<Pal>> ReaderFactory::createReader<Pal>(Format format) {
    switch (format) {
    case Format::PAL:
        return std::make_unique<PalReader>();
    default:
        throw UnsupportedFormatException("Invalid format for Pal reader");
    }
}

template<>
std::unique_ptr<FileParser<Gam>> ReaderFactory::createReader<Gam>(Format format) {
    switch (format) {
    case Format::GAM:
        return std::make_unique<GamReader>();
    default:
        throw UnsupportedFormatException("Invalid format for Gam reader");
    }
}

template<>
std::unique_ptr<FileParser<Msg>> ReaderFactory::createReader<Msg>(Format format) {
    switch (format) {
    case Format::MSG:
        return std::make_unique<MsgReader>();
    default:
        throw UnsupportedFormatException("Invalid format for Msg reader");
    }
}

template<>
std::unique_ptr<FileParser<Lst>> ReaderFactory::createReader<Lst>(Format format) {
    switch (format) {
    case Format::LST:
        return std::make_unique<LstReader>();
    default:
        throw UnsupportedFormatException("Invalid format for Lst reader");
    }
}

// Map reader not supported - requires callback parameter

// Legacy interface support methods
void* ReaderFactory::createGenericReader(const std::filesystem::path& filePath, Format& detectedFormat) {
    detectedFormat = detectFormat(filePath);
    
    switch (detectedFormat) {
    case Format::DAT: return createReader<Dat>(detectedFormat).release();
    case Format::PRO: return createReader<Pro>(detectedFormat).release();
    case Format::FRM: return createReader<Frm>(detectedFormat).release();
    case Format::PAL: return createReader<Pal>(detectedFormat).release();
    case Format::GAM: return createReader<Gam>(detectedFormat).release();
    case Format::MSG: return createReader<Msg>(detectedFormat).release();
    case Format::LST: return createReader<Lst>(detectedFormat).release();
    case Format::MAP: throw UnsupportedFormatException("Map reader requires callback - use MapReader constructor directly");
    default:
        throw UnsupportedFormatException("Unsupported file format: " + filePath.filename().string());
    }
}

void* ReaderFactory::createGenericReader(const std::vector<uint8_t>& data, const std::string& filename, Format& detectedFormat) {
    detectedFormat = detectFormat(data, filename);
    
    switch (detectedFormat) {
    case Format::DAT: return createReader<Dat>(detectedFormat).release();
    case Format::PRO: return createReader<Pro>(detectedFormat).release();
    case Format::FRM: return createReader<Frm>(detectedFormat).release();
    case Format::PAL: return createReader<Pal>(detectedFormat).release();
    case Format::GAM: return createReader<Gam>(detectedFormat).release();
    case Format::MSG: return createReader<Msg>(detectedFormat).release();
    case Format::LST: return createReader<Lst>(detectedFormat).release();
    case Format::MAP: throw UnsupportedFormatException("Map reader requires callback - use MapReader constructor directly");
    default:
        throw UnsupportedFormatException("Unsupported file format: " + filename);
    }
}

// Format detection methods
ReaderFactory::Format ReaderFactory::detectFormat(const std::filesystem::path& filePath) {
    // First try extension-based detection
    Format extensionFormat = detectByExtension(filePath);
    if (extensionFormat != Format::UNKNOWN) {
        return extensionFormat;
    }
    
    // If extension detection fails, try content-based detection
    try {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            spdlog::warn("ReaderFactory: Cannot open file for format detection: {}", filePath.string());
            return Format::UNKNOWN;
        }
        
        // Read first few bytes for magic number detection
        constexpr size_t BUFFER_SIZE = 512;
        std::vector<uint8_t> buffer(BUFFER_SIZE);
        file.read(reinterpret_cast<char*>(buffer.data()), BUFFER_SIZE);
        size_t bytes_read = file.gcount();
        buffer.resize(bytes_read);
        
        Format magicFormat = detectByMagicNumber(buffer);
        if (magicFormat != Format::UNKNOWN) {
            return magicFormat;
        }
        
        return detectByContent(buffer);
    } catch (const std::exception& e) {
        spdlog::error("ReaderFactory: Error during format detection for {}: {}", filePath.string(), e.what());
        return Format::UNKNOWN;
    }
}

ReaderFactory::Format ReaderFactory::detectFormat(const std::vector<uint8_t>& data, const std::string& filename) {
    // First try extension-based detection from filename
    std::filesystem::path path(filename);
    Format extensionFormat = detectByExtension(path);
    if (extensionFormat != Format::UNKNOWN) {
        return extensionFormat;
    }
    
    // If extension detection fails, try content-based detection
    Format magicFormat = detectByMagicNumber(data);
    if (magicFormat != Format::UNKNOWN) {
        return magicFormat;
    }
    
    return detectByContent(data);
}

ReaderFactory::Format ReaderFactory::detectByExtension(const std::filesystem::path& filePath) {
    std::string extension = filePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    auto it = extension_format_map.find(extension);
    return (it != extension_format_map.end()) ? it->second : Format::UNKNOWN;
}

ReaderFactory::Format ReaderFactory::detectByMagicNumber(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return Format::UNKNOWN;
    }
    
    for (const auto& [format, info] : format_info_map) {
        if (!info.magic_signature.empty() && 
            data.size() >= info.signature_offset + info.magic_signature.size()) {
            
            bool matches = std::equal(
                info.magic_signature.begin(),
                info.magic_signature.end(),
                data.begin() + info.signature_offset
            );
            
            if (matches) {
                return format;
            }
        }
    }
    
    return Format::UNKNOWN;
}

ReaderFactory::Format ReaderFactory::detectByContent(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return Format::UNKNOWN;
    }
    
    // Content-based heuristics for formats without clear magic numbers
    
    // PAL files are exactly 768 bytes (256 colors * 3 channels)
    if (data.size() == 768) {
        return Format::PAL;
    }
    
    // MSG files typically start with lines in format {number}{}
    if (data.size() > 10) {
        std::string start(data.begin(), data.begin() + std::min(size_t(10), data.size()));
        if (start.find("{") != std::string::npos && start.find("}") != std::string::npos) {
            return Format::MSG;
        }
    }
    
    // LST files contain text with file names, typically one per line
    if (data.size() > 4) {
        // Check if data contains mostly printable ASCII characters
        size_t printable_count = 0;
        size_t total_count = std::min(data.size(), size_t(100)); // Check first 100 bytes
        
        for (size_t i = 0; i < total_count; ++i) {
            if (std::isprint(data[i]) || std::isspace(data[i])) {
                printable_count++;
            }
        }
        
        // If more than 80% are printable, likely text file (LST)
        if (printable_count > (total_count * 4 / 5)) {
            return Format::LST;
        }
    }
    
    // PRO files have specific structure - check size and some known patterns
    if (data.size() >= 80) { // Minimum PRO size
        // PRO files typically have specific patterns in first few bytes
        // This is a simple heuristic and might need refinement
        return Format::PRO;
    }
    
    return Format::UNKNOWN;
}

double ReaderFactory::calculateConfidence(Format format, const std::vector<uint8_t>& data) {
    // This is a simplified confidence calculation
    // In a more sophisticated implementation, this would analyze the data
    return (format != Format::UNKNOWN) ? 0.8 : 0.0;
}

ReaderFactory::FormatInfo ReaderFactory::getFormatInfo(Format format) {
    auto it = format_info_map.find(format);
    if (it != format_info_map.end()) {
        return it->second;
    }
    return {Format::UNKNOWN, "Unknown", {}, {}, 0, 0, 0.0};
}

std::vector<ReaderFactory::FormatInfo> ReaderFactory::getAllSupportedFormats() {
    std::vector<FormatInfo> formats;
    for (const auto& [format, info] : format_info_map) {
        formats.push_back(info);
    }
    return formats;
}

bool ReaderFactory::isFormatSupported(Format format) {
    return format_info_map.find(format) != format_info_map.end();
}

bool ReaderFactory::isFormatSupported(const std::string& extension) {
    std::string lower_ext = extension;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), ::tolower);
    return extension_format_map.find(lower_ext) != extension_format_map.end();
}

std::vector<std::string> ReaderFactory::getSupportedExtensions() {
    std::vector<std::string> extensions;
    for (const auto& [ext, format] : extension_format_map) {
        extensions.push_back(ext);
    }
    return extensions;
}

std::map<ReaderFactory::Format, std::vector<std::filesystem::path>> 
ReaderFactory::groupFilesByFormat(const std::vector<std::filesystem::path>& filePaths) {
    std::map<Format, std::vector<std::filesystem::path>> groups;
    
    for (const auto& path : filePaths) {
        Format format = detectFormat(path);
        groups[format].push_back(path);
    }
    
    return groups;
}

} // namespace geck