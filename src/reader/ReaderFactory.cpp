#include "ReaderFactory.h"

// Reader implementations
#include "pro/ProReader.h"
#include "frm/FrmReader.h"
#include "pal/PalReader.h"
#include "gam/GamReader.h"
#include "msg/MsgReader.h"
#include "lst/LstReader.h"
#include "reader/TextParsing.h"

#include <algorithm>
#include <string>

namespace geck {

// Split critter animations store one direction per file as .fr0-.fr5 (fallout2-ce art.cc).
const std::map<std::string, ReaderFactory::Format> ReaderFactory::extension_format_map = {
    { ".pro", Format::PRO },
    { ".frm", Format::FRM },
    { ".fr0", Format::FRM },
    { ".fr1", Format::FRM },
    { ".fr2", Format::FRM },
    { ".fr3", Format::FRM },
    { ".fr4", Format::FRM },
    { ".fr5", Format::FRM },
    { ".pal", Format::PAL },
    { ".gam", Format::GAM },
    { ".msg", Format::MSG },
    { ".lst", Format::LST }
};

template <>
std::unique_ptr<FileParser<Pro>> ReaderFactory::createReader<Pro>(Format format) {
    switch (format) {
        case Format::PRO:
            return std::make_unique<ProReader>();
        default:
            throw UnsupportedFormatException("Invalid format for Pro reader");
    }
}

template <>
std::unique_ptr<FileParser<Frm>> ReaderFactory::createReader<Frm>(Format format) {
    switch (format) {
        case Format::FRM:
            return std::make_unique<FrmReader>();
        default:
            throw UnsupportedFormatException("Invalid format for Frm reader");
    }
}

template <>
std::unique_ptr<FileParser<Pal>> ReaderFactory::createReader<Pal>(Format format) {
    switch (format) {
        case Format::PAL:
            return std::make_unique<PalReader>();
        default:
            throw UnsupportedFormatException("Invalid format for Pal reader");
    }
}

template <>
std::unique_ptr<FileParser<Gam>> ReaderFactory::createReader<Gam>(Format format) {
    switch (format) {
        case Format::GAM:
            return std::make_unique<GamReader>();
        default:
            throw UnsupportedFormatException("Invalid format for Gam reader");
    }
}

template <>
std::unique_ptr<FileParser<Msg>> ReaderFactory::createReader<Msg>(Format format) {
    switch (format) {
        case Format::MSG:
            return std::make_unique<MsgReader>();
        default:
            throw UnsupportedFormatException("Invalid format for Msg reader");
    }
}

template <>
std::unique_ptr<FileParser<Lst>> ReaderFactory::createReader<Lst>(Format format) {
    switch (format) {
        case Format::LST:
            return std::make_unique<LstReader>();
        default:
            throw UnsupportedFormatException("Invalid format for Lst reader");
    }
}

ReaderFactory::Format ReaderFactory::detectByExtension(const std::filesystem::path& filePath) {
    const std::string extension = text::toLower(filePath.extension().string());

    auto it = extension_format_map.find(extension);
    return (it != extension_format_map.end()) ? it->second : Format::UNKNOWN;
}

} // namespace geck
