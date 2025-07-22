#include "ReaderFactory.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>

// Include all reader headers
#include "dat/DatReader.h"
#include "pro/ProReader.h"
#include "frm/FrmReader.h"
#include "pal/PalReader.h"
#include "gam/GamReader.h"
#include "msg/MsgReader.h"
#include "lst/LstReader.h"
#include "map/MapReader.h"

// Include format headers
#include "format/dat/Dat.h"
#include "format/pro/Pro.h"
#include "format/frm/Frm.h"
#include "format/pal/Pal.h"
#include "format/gam/Gam.h"
#include "format/msg/Msg.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"

namespace geck {

// Static initialization of format information
const std::map<ReaderFactory::Format, ReaderFactory::FormatInfo> ReaderFactory::format_info_map = {
    {Format::DAT, {Format::DAT, "Fallout DAT Archive", {".dat"}, {}, 0, 8, 0.0}},
    {Format::PRO, {Format::PRO, "Fallout PRO Object", {".pro"}, {}, 0, 24, 0.0}},
    {Format::FRM, {Format::FRM, "Fallout FRM Animation", {".frm"}, {0x00, 0x00, 0x00, 0x04}, 0, 62, 0.0}}, // Version 4
    {Format::PAL, {Format::PAL, "Fallout PAL Palette", {".pal"}, {}, 0, 0x8300, 0.0}}, // Fixed size
    {Format::GAM, {Format::GAM, "Fallout GAM Save", {".gam"}, {}, 0, 10, 0.0}}, // Text format, minimal size
    {Format::MSG, {Format::MSG, "Fallout MSG Messages", {".msg"}, {}, 0, 1, 0.0}}, // Text format, minimal size
    {Format::LST, {Format::LST, "Fallout LST List", {".lst"}, {}, 0, 0, 0.0}}, // Text format, can be empty
    {Format::MAP, {Format::MAP, "Fallout MAP File", {".map"}, {}, 0, 8, 0.0}} // Has version field
};

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
    if (format != Format::DAT) {
        throw UnsupportedFormatException("Format mismatch: expected DAT format");
    }
    return std::make_unique<DatReader>();
}

template<>
std::unique_ptr<FileParser<Pro>> ReaderFactory::createReader<Pro>(Format format) {
    if (format != Format::PRO) {
        throw UnsupportedFormatException("Format mismatch: expected PRO format");
    }
    return std::make_unique<ProReader>();
}

template<>
std::unique_ptr<FileParser<Frm>> ReaderFactory::createReader<Frm>(Format format) {
    if (format != Format::FRM) {
        throw UnsupportedFormatException("Format mismatch: expected FRM format");
    }
    return std::make_unique<FrmReader>();
}

template<>
std::unique_ptr<FileParser<Pal>> ReaderFactory::createReader<Pal>(Format format) {
    if (format != Format::PAL) {
        throw UnsupportedFormatException("Format mismatch: expected PAL format");
    }
    return std::make_unique<PalReader>();
}

template<>
std::unique_ptr<FileParser<Gam>> ReaderFactory::createReader<Gam>(Format format) {
    if (format != Format::GAM) {
        throw UnsupportedFormatException("Format mismatch: expected GAM format");
    }
    return std::make_unique<GamReader>();
}

template<>
std::unique_ptr<FileParser<Msg>> ReaderFactory::createReader<Msg>(Format format) {
    if (format != Format::MSG) {
        throw UnsupportedFormatException("Format mismatch: expected MSG format");
    }
    return std::make_unique<MsgReader>();
}

template<>
std::unique_ptr<FileParser<Lst>> ReaderFactory::createReader<Lst>(Format format) {
    if (format != Format::LST) {
        throw UnsupportedFormatException("Format mismatch: expected LST format");
    }
    return std::make_unique<LstReader>();
}

template<>
std::unique_ptr<FileParser<Map>> ReaderFactory::createReader<Map>(Format format) {
    if (format != Format::MAP) {
        throw UnsupportedFormatException("Format mismatch: expected MAP format");
    }
    // MapReader requires a callback - provide default null callback
    return std::make_unique<MapReader>([](uint32_t) -> Pro* { return nullptr; });
}

// Generic reader creation with format detection
void* ReaderFactory::createGenericReader(const std::filesystem::path& filePath, Format& detectedFormat) {
    detectedFormat = detectFormat(filePath);
    
    switch (detectedFormat) {
        case Format::DAT: return new DatReader();
        case Format::PRO: return new ProReader();
        case Format::FRM: return new FrmReader();
        case Format::PAL: return new PalReader();
        case Format::GAM: return new GamReader();
        case Format::MSG: return new MsgReader();
        case Format::LST: return new LstReader();
        case Format::MAP: 
            // MapReader requires a callback - provide default null callback
            return new MapReader([](uint32_t) -> Pro* { return nullptr; });
        default:
            throw UnsupportedFormatException("Unknown or unsupported file format: " + filePath.string());
    }
}

void* ReaderFactory::createGenericReader(const std::vector<uint8_t>& data, const std::string& filename, Format& detectedFormat) {
    detectedFormat = detectFormat(data, filename);
    
    switch (detectedFormat) {
        case Format::DAT: return new DatReader();
        case Format::PRO: return new ProReader();
        case Format::FRM: return new FrmReader();
        case Format::PAL: return new PalReader();
        case Format::GAM: return new GamReader();
        case Format::MSG: return new MsgReader();
        case Format::LST: return new LstReader();
        case Format::MAP:
            // MapReader requires a callback - provide default null callback
            return new MapReader([](uint32_t) -> Pro* { return nullptr; });
        default:
            throw UnsupportedFormatException("Unknown or unsupported format for data: " + filename);
    }
}

// Format detection methods
ReaderFactory::Format ReaderFactory::detectFormat(const std::filesystem::path& filePath) {
    spdlog::trace("Detecting format for file: {}", filePath.string());
    
    // First, try by extension
    Format format = detectByExtension(filePath);
    if (format != Format::UNKNOWN) {
        spdlog::debug("Format detected by extension: {}", filePath.extension().string());
        return format;
    }
    
    // If extension detection fails, read file and analyze content
    try {
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            throw IOException("Could not open file for format detection", filePath);
        }
        
        // Read first 1KB for analysis
        std::vector<uint8_t> data(1024);
        file.read(reinterpret_cast<char*>(data.data()), data.size());
        data.resize(file.gcount());
        
        format = detectFormat(data, filePath.filename().string());
        
    } catch (const std::exception& e) {
        spdlog::warn("Failed to analyze file content for format detection: {}", e.what());
        return Format::UNKNOWN;
    }
    
    return format;
}

ReaderFactory::Format ReaderFactory::detectFormat(const std::vector<uint8_t>& data, const std::string& filename) {
    spdlog::trace("Detecting format for data buffer, filename: {}", filename);
    
    // Try extension first if filename provided
    if (!filename.empty()) {
        std::filesystem::path path(filename);
        Format format = detectByExtension(path);
        if (format != Format::UNKNOWN) {
            spdlog::debug("Format detected by filename extension: {}", path.extension().string());
            return format;
        }
    }
    
    // Try magic number detection
    Format format = detectByMagicNumber(data);
    if (format != Format::UNKNOWN) {
        spdlog::debug("Format detected by magic number");
        return format;
    }
    
    // Try content-based detection
    format = detectByContent(data);
    spdlog::debug("Format detection result: {}", static_cast<int>(format));
    
    return format;
}

ReaderFactory::Format ReaderFactory::detectByExtension(const std::filesystem::path& filePath) {
    std::string ext = filePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    auto it = extension_format_map.find(ext);
    return (it != extension_format_map.end()) ? it->second : Format::UNKNOWN;
}

ReaderFactory::Format ReaderFactory::detectByMagicNumber(const std::vector<uint8_t>& data) {
    // Check FRM magic number (version 4 at start)
    if (data.size() >= 4) {
        if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x04) {
            return Format::FRM;
        }
    }
    
    // DAT files have their signature at the end, need full file for proper detection
    // For now, we'll rely on extension or content analysis
    
    return Format::UNKNOWN;
}

ReaderFactory::Format ReaderFactory::detectByContent(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return Format::UNKNOWN;
    }
    
    // Check for text-based formats
    bool is_text = std::all_of(data.begin(), data.end(), [](uint8_t byte) {
        return byte == '\n' || byte == '\r' || byte == '\t' || 
               (byte >= 32 && byte <= 126); // Printable ASCII
    });
    
    if (is_text) {
        std::string content(data.begin(), data.end());
        
        // Check for GAM format patterns
        if (content.find("GAME_GLOBAL_VARS:") != std::string::npos ||
            content.find("MAP_GLOBAL_VARS:") != std::string::npos) {
            return Format::GAM;
        }
        
        // Check for MSG format patterns
        if (content.find("{") != std::string::npos && content.find("}") != std::string::npos) {
            // MSG files have {id}{audio}{text} format
            std::regex msg_pattern(R"(\{\d+\}\{.*?\}\{.*?\})");
            if (std::regex_search(content, msg_pattern)) {
                return Format::MSG;
            }
        }
        
        // LST files are simple text lists
        return Format::LST;
    }
    
    // Check for binary formats based on size and structure
    
    // PAL files have a fixed size
    if (data.size() >= 0x8300) {
        return Format::PAL;
    }
    
    // PRO files have minimum size
    if (data.size() >= 24) {
        return Format::PRO;
    }
    
    // MAP files typically start with a version number
    if (data.size() >= 4) {
        uint32_t possible_version = 
            (static_cast<uint32_t>(data[0]) << 24) |
            (static_cast<uint32_t>(data[1]) << 16) |
            (static_cast<uint32_t>(data[2]) << 8) |
            static_cast<uint32_t>(data[3]);
        
        if (possible_version >= 19 && possible_version <= 21) {
            return Format::MAP;
        }
    }
    
    return Format::UNKNOWN;
}

double ReaderFactory::calculateConfidence(Format format, const std::vector<uint8_t>& data) {
    (void)data; // Suppress unused parameter warning
    // This could be expanded to provide confidence scores for format detection
    // For now, return simple binary confidence
    return (format != Format::UNKNOWN) ? 1.0 : 0.0;
}

// Utility methods
ReaderFactory::FormatInfo ReaderFactory::getFormatInfo(Format format) {
    auto it = format_info_map.find(format);
    if (it != format_info_map.end()) {
        return it->second;
    }
    return {Format::UNKNOWN, "Unknown Format", {}, {}, 0, 0, 0.0};
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
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return extension_format_map.find(ext) != extension_format_map.end();
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
    
    for (const auto& filePath : filePaths) {
        Format format = detectFormat(filePath);
        groups[format].push_back(filePath);
    }
    
    return groups;
}

} // namespace geck