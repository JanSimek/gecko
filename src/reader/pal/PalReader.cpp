#include "PalReader.h"

#include <array>
#include <spdlog/spdlog.h>

#include "../../format/pal/Pal.h"
#include "../ErrorMessages.h"

namespace geck {

std::unique_ptr<geck::Pal> geck::PalReader::read() {
    try {
        auto& utils = getBinaryUtils();
        spdlog::debug("Reading PAL file: {}", _path.string());
        
        constexpr size_t EXPECTED_FILESIZE = 0x00008300; // 33,536 bytes
        
        // Validate file size
        auto pos = utils.getPosition();
        if (pos.total < EXPECTED_FILESIZE) {
            throw UnsupportedFormatException(
                ErrorMessages::invalidFileSize(_path, pos.total, EXPECTED_FILESIZE), _path);
        }
        
        spdlog::trace("PAL file size validation passed: {} bytes", pos.total);
        
        // Read the entire palette data as a block
        std::array<uint8_t, EXPECTED_FILESIZE> buf;
        read_bytes(buf.data(), buf.size());

        auto pal = std::make_unique<Pal>(_path);

        // Parse palette data from buffer
        size_t offset = 0;
        
        // Read RGB palette (256 colors * 3 bytes each = 768 bytes)
        spdlog::trace("Reading RGB palette data (768 bytes)");
        for (Rgb& rgb : pal->palette()) {
            if (offset + 2 >= EXPECTED_FILESIZE) {
                throw ParseException(
                    ErrorMessages::corruptedData(_path, "RGB palette data truncated"), _path);
            }
            rgb.r = buf[offset++];
            rgb.g = buf[offset++];
            rgb.b = buf[offset++];
        }
        
        spdlog::trace("Reading RGB conversion table ({} bytes)", EXPECTED_FILESIZE - offset);
        
        // Read RGB conversion table (remaining bytes)
        for (uint8_t& conversion : pal->rgbConversionTable()) {
            if (offset >= EXPECTED_FILESIZE) {
                throw ParseException(
                    ErrorMessages::corruptedData(_path, "RGB conversion table truncated"), _path);
            }
            conversion = buf[offset++];
            
            // Validate conversion table values (should be palette indices 0-255)
            if (conversion > 255) {
                spdlog::warn("PAL file has unusual conversion table value: {} at offset {}", 
                           conversion, offset - 1);
            }
        }
        
        spdlog::debug("Successfully read PAL file: {} colors, {} conversion entries", 
                     pal->palette().size(), pal->rgbConversionTable().size());
        
        // TODO: Parse static colors (monitors, slime, fire, etc.)
        // Reference: https://github.com/falltergeist/falltergeist/blob/39a6cee826c4588cac10919786c939f222684b94/src/Format/Pal/File.cpp
        
        return pal;
        
    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException(
            "Failed to parse PAL file: " + std::string(e.what()), _path);
    }
}

} // namespace geck
