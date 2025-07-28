#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <algorithm>
#include <spdlog/spdlog.h>

#include "ReaderExceptions.h"
#include "BinaryUtils.h"
#include "ErrorMessages.h"

namespace geck {

struct FormatSignature {
    std::vector<uint8_t> signature;
    size_t offset;
    std::string description;
    
    FormatSignature(std::vector<uint8_t> sig, size_t off, std::string desc)
        : signature(std::move(sig)), offset(off), description(std::move(desc)) {}
};

class FormatValidator {
public:
    static void validateDatFile(BinaryUtils& utils, const std::filesystem::path& filePath) {
        // DAT files have their signature at the end
        auto pos = utils.getPosition();
        size_t fileSize = pos.total;
        
        if (fileSize < 8) {
            throw UnsupportedFormatException(
                ErrorMessages::invalidFileSize(filePath, fileSize, 8), filePath);
        }
        
        // Go to footer (last 8 bytes)
        utils.setPosition(fileSize - 8);
        uint32_t treeSize = utils.readBE32();
        uint32_t dataSize = utils.readBE32();
        
        if (dataSize != fileSize) {
            throw UnsupportedFormatException(
                ErrorMessages::datFileError(filePath, 
                    "File size mismatch: header says " + std::to_string(dataSize) + 
                    " bytes, actual file is " + std::to_string(fileSize) + " bytes"), filePath);
        }
        
        if (treeSize == 0 || treeSize > fileSize) {
            throw UnsupportedFormatException(
                ErrorMessages::datFileError(filePath, 
                    "Invalid tree size: " + std::to_string(treeSize)), filePath);
        }
        
        // Restore original position
        utils.setPosition(pos.current);
        spdlog::debug("DAT format validation passed: {} files", treeSize);
    }
    
    static void validateProFile(BinaryUtils& utils, const std::filesystem::path& filePath) {
        auto pos = utils.getPosition();
        
        if (pos.total < 24) { // Minimum PRO header size
            throw UnsupportedFormatException(
                ErrorMessages::invalidFileSize(filePath, pos.total, 24), filePath);
        }

        int32_t pid = utils.readBE32Signed();

        utils.setPosition(0);
        spdlog::debug("PRO format validation passed: PID=0x{:08X}", pid);
    }
    
    static void validateFrmFile(BinaryUtils& utils, const std::filesystem::path& filePath) {
        auto pos = utils.getPosition();
        
        if (pos.total < 62) { // Minimum FRM header size
            throw UnsupportedFormatException("File too small to be a valid FRM file", filePath);
        }
        
        // Read version
        uint32_t version = utils.readBE32();
        if (version != 4) {
            throw UnsupportedFormatException(
                "Unsupported FRM version: " + std::to_string(version), filePath);
        }

        // Reset to start
        utils.setPosition(0);
        spdlog::debug("FRM format validation passed: version={}", version);
    }
    
    static void validateMsgFile(BinaryUtils& utils, const std::filesystem::path& filePath) {
        auto pos = utils.getPosition();
        
        if (pos.total == 0) {
            throw UnsupportedFormatException("Empty MSG file", filePath);
        }
        
        // MSG files should start with a number or '{' for comments
        utils.setPosition(0);
        uint8_t firstByte = utils.readU8();
        
        if (firstByte != '{' && (firstByte < '0' || firstByte > '9')) {
            throw UnsupportedFormatException("MSG file has invalid header", filePath);
        }
        
        utils.setPosition(0);
        spdlog::debug("MSG format validation passed");
    }
    
    static void validateLstFile(BinaryUtils& utils, const std::filesystem::path& filePath) {
        auto pos = utils.getPosition();
        
        if (pos.total == 0) {
            spdlog::warn("Empty LST file: {}", filePath.string());
            return;
        }
        
        // LST files are text files, should contain printable ASCII
        utils.setPosition(0);
        
        // Check first few bytes for printable characters
        size_t checkBytes = std::min(pos.total, size_t(64));
        for (size_t i = 0; i < checkBytes; ++i) {
            uint8_t byte = utils.readU8();
            if (byte != '\r' && byte != '\n' && byte != '\t' && 
                (byte < 32 || byte > 126)) {
                throw UnsupportedFormatException("LST file contains non-text data", filePath);
            }
        }
        
        utils.setPosition(0);
        spdlog::debug("LST format validation passed");
    }
    
    static void validateMapFile(BinaryUtils& utils, const std::filesystem::path& filePath) {
        auto pos = utils.getPosition();
        
        if (pos.total < 8) {
            throw UnsupportedFormatException("File too small to be a valid MAP file", filePath);
        }
        
        // Read version
        uint32_t version = utils.readBE32();
        if (version < 19 || version > 21) { // Known Fallout map versions
            spdlog::warn("Unusual MAP version: {}", version);
        }
        
        utils.setPosition(0);
        spdlog::debug("MAP format validation passed: version={}", version);
    }
    
    // Generic validation helper
    static void validateFileExtension(const std::filesystem::path& filePath, 
                                    const std::vector<std::string>& validExtensions) {
        std::string ext = filePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        for (const auto& validExt : validExtensions) {
            if (ext == validExt) {
                return;
            }
        }
        
        throw UnsupportedFormatException(
            "Invalid file extension '" + ext + "' for this format", filePath);
    }
    
    // Helper to get format info for debugging
    struct FormatInfo {
        std::string formatName;
        uint32_t version = 0;
        size_t fileSize = 0;
        std::map<std::string, std::string> metadata;
    };
    
    static FormatInfo getFormatInfo(BinaryUtils& utils, const std::filesystem::path& filePath) {
        FormatInfo info;
        info.fileSize = utils.getPosition().total;
        
        std::string ext = filePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".dat") {
            info.formatName = "Fallout DAT Archive";
            // Could extract more metadata here
        } else if (ext == ".pro") {
            info.formatName = "Fallout PRO Object";
        } else if (ext == ".frm") {
            info.formatName = "Fallout FRM Animation";
        } else if (ext == ".map") {
            info.formatName = "Fallout MAP File";
        } else {
            info.formatName = "Unknown Format";
        }
        
        return info;
    }
};

} // namespace geck