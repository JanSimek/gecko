#include "WriterFactory.h"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "map/MapWriter.h"
#include "../format/map/Map.h"
#include "../format/pro/Pro.h"
#include "../format/frm/Frm.h"
#include "../format/msg/Msg.h"
#include "../format/lst/Lst.h"
#include "../format/pal/Pal.h"
#include "../format/gam/Gam.h"

namespace geck {

// Static member definitions
const std::unordered_map<std::string, WriterFactory::Format> WriterFactory::_extensionMap = {
    {".map", Format::MAP},
    {".pro", Format::PRO},
    {".frm", Format::FRM},
    {".frm0", Format::FRM}, {".frm1", Format::FRM}, {".frm2", Format::FRM},
    {".frm3", Format::FRM}, {".frm4", Format::FRM}, {".frm5", Format::FRM},
    {".msg", Format::MSG},
    {".lst", Format::LST},
    {".pal", Format::PAL},
    {".gam", Format::GAM},
    {".dat", Format::DAT}
};

const std::unordered_map<WriterFactory::Format, WriterFactory::FormatInfo> WriterFactory::_formatInfoMap = {
    {Format::MAP, {Format::MAP, "Fallout MAP File", {".map"}, "Map layout and object placement", 50000, false}},
    {Format::PRO, {Format::PRO, "Fallout PRO Object", {".pro"}, "Object prototype definitions", 150, false}},
    {Format::FRM, {Format::FRM, "Fallout FRM Animation", {".frm", ".frm0", ".frm1", ".frm2", ".frm3", ".frm4", ".frm5"}, "Sprite animation frames", 5000, false}},
    {Format::MSG, {Format::MSG, "Fallout MSG Messages", {".msg"}, "Game text and dialogue", 2000, false}},
    {Format::LST, {Format::LST, "Fallout LST List", {".lst"}, "File lists and indexes", 500, false}},
    {Format::PAL, {Format::PAL, "Fallout PAL Palette", {".pal"}, "256-color palette", 768, false}},
    {Format::GAM, {Format::GAM, "Fallout GAM Save", {".gam"}, "Game save data", 10000, false}},
    {Format::DAT, {Format::DAT, "Fallout DAT Archive", {".dat"}, "Compressed file archive", 50000000, true}},
    {Format::UNKNOWN, {Format::UNKNOWN, "Unknown Format", {}, "Unsupported file format", 0, false}}
};

WriterFactory::Format WriterFactory::detectFormat(const std::filesystem::path& filePath) {
    std::string extension = filePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    auto it = _extensionMap.find(extension);
    if (it != _extensionMap.end()) {
        return it->second;
    }
    
    spdlog::warn("Unknown file extension for writing: {}", extension);
    return Format::UNKNOWN;
}

WriterFactory::Format WriterFactory::detectFormat(const std::string& extension) {
    std::string lowerExt = extension;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
    
    if (lowerExt.empty() || lowerExt[0] != '.') {
        lowerExt = '.' + lowerExt;
    }
    
    auto it = _extensionMap.find(lowerExt);
    return (it != _extensionMap.end()) ? it->second : Format::UNKNOWN;
}

bool WriterFactory::isFormatSupported(Format format) {
    return format != Format::UNKNOWN && _formatInfoMap.find(format) != _formatInfoMap.end();
}

bool WriterFactory::isFormatSupported(const std::string& extension) {
    return detectFormat(extension) != Format::UNKNOWN;
}

std::vector<std::string> WriterFactory::getSupportedExtensions() {
    std::vector<std::string> extensions;
    for (const auto& [ext, format] : _extensionMap) {
        if (format != Format::UNKNOWN) {
            extensions.push_back(ext);
        }
    }
    std::sort(extensions.begin(), extensions.end());
    return extensions;
}

WriterFactory::FormatInfo WriterFactory::getFormatInfo(Format format) {
    auto it = _formatInfoMap.find(format);
    return (it != _formatInfoMap.end()) ? it->second : _formatInfoMap.at(Format::UNKNOWN);
}

std::vector<WriterFactory::FormatInfo> WriterFactory::getAllSupportedFormats() {
    std::vector<FormatInfo> formats;
    for (const auto& [format, info] : _formatInfoMap) {
        if (format != Format::UNKNOWN) {
            formats.push_back(info);
        }
    }
    return formats;
}

std::unordered_map<WriterFactory::Format, std::vector<std::filesystem::path>> 
WriterFactory::groupFilesByFormat(const std::vector<std::filesystem::path>& files) {
    std::unordered_map<Format, std::vector<std::filesystem::path>> groups;
    
    for (const auto& file : files) {
        Format format = detectFormat(file);
        groups[format].push_back(file);
    }
    
    return groups;
}

std::unique_ptr<void> WriterFactory::createGenericWriter(Format format) {
    switch (format) {
        case Format::MAP:
            // MapWriter requires callback, so we can't create it generically
            throw FormatWriteException("MapWriter requires callback parameter", "");
        case Format::PRO:
            // Will be implemented when ProWriter is created
            throw FormatWriteException("ProWriter not yet implemented", "");
        case Format::FRM:
            // Will be implemented when FrmWriter is created  
            throw FormatWriteException("FrmWriter not yet implemented", "");
        case Format::MSG:
            // Will be implemented when MsgWriter is created
            throw FormatWriteException("MsgWriter not yet implemented", "");
        case Format::LST:
            // Will be implemented when LstWriter is created
            throw FormatWriteException("LstWriter not yet implemented", "");
        case Format::PAL:
            // Will be implemented when PalWriter is created
            throw FormatWriteException("PalWriter not yet implemented", "");
        case Format::GAM:
            // Will be implemented when GamWriter is created
            throw FormatWriteException("GamWriter not yet implemented", "");
        case Format::DAT:
            // Will be implemented when DatWriter is created
            throw FormatWriteException("DatWriter not yet implemented", "");
        default:
            throw FormatWriteException("Unknown format for writer creation", "");
    }
}

// Template specializations

template<>
std::unique_ptr<FileWriter<Map::MapFile>> WriterFactory::createWriter<Map::MapFile>(Format format) {
    VALIDATE_WRITER_FORMAT(Format::MAP, format, "Map::MapFile");
    
    // MapWriter requires a callback for loading PRO files, so we can't create it without parameters
    throw FormatWriteException("MapWriter requires loadProCallback parameter. Use createWriterWithCallback instead.", "");
}

template<>
std::unique_ptr<FileWriter<Pro>> WriterFactory::createWriter<Pro>(Format format) {
    VALIDATE_WRITER_FORMAT(Format::PRO, format, "Pro");
    
    // Will be implemented when ProWriter is created
    throw FormatWriteException("ProWriter not yet implemented", "");
}

template<>
std::unique_ptr<FileWriter<Frm>> WriterFactory::createWriter<Frm>(Format format) {
    VALIDATE_WRITER_FORMAT(Format::FRM, format, "Frm");
    
    // Will be implemented when FrmWriter is created
    throw FormatWriteException("FrmWriter not yet implemented", "");
}

template<>
std::unique_ptr<FileWriter<Msg>> WriterFactory::createWriter<Msg>(Format format) {
    VALIDATE_WRITER_FORMAT(Format::MSG, format, "Msg");
    
    // Will be implemented when MsgWriter is created
    throw FormatWriteException("MsgWriter not yet implemented", "");
}

template<>
std::unique_ptr<FileWriter<Lst>> WriterFactory::createWriter<Lst>(Format format) {
    VALIDATE_WRITER_FORMAT(Format::LST, format, "Lst");
    
    // Will be implemented when LstWriter is created
    throw FormatWriteException("LstWriter not yet implemented", "");
}

template<>
std::unique_ptr<FileWriter<Pal>> WriterFactory::createWriter<Pal>(Format format) {
    VALIDATE_WRITER_FORMAT(Format::PAL, format, "Pal");
    
    // Will be implemented when PalWriter is created
    throw FormatWriteException("PalWriter not yet implemented", "");
}

template<>
std::unique_ptr<FileWriter<Gam>> WriterFactory::createWriter<Gam>(Format format) {
    VALIDATE_WRITER_FORMAT(Format::GAM, format, "Gam");
    
    // Will be implemented when GamWriter is created
    throw FormatWriteException("GamWriter not yet implemented", "");
}

// Helper function for MapWriter creation with callback
template<>
std::unique_ptr<FileWriter<Map::MapFile>> WriterFactory::createWriterWithCallback<Map::MapFile>(
    Format format, std::function<void*(uint32_t)> callback) {
    
    VALIDATE_WRITER_FORMAT(Format::MAP, format, "Map::MapFile");
    
    // Cast the generic callback to the specific type needed by MapWriter
    auto proCallback = [callback](int32_t PID) -> Pro* {
        return static_cast<Pro*>(callback(static_cast<uint32_t>(PID)));
    };
    
    return std::make_unique<MapWriter>(proCallback);
}

} // namespace geck