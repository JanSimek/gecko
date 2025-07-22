#include "FrmReader.h"

#include <spdlog/spdlog.h>

#include "../../format/frm/Direction.h"
#include "../../format/frm/Frame.h"
#include "../../format/frm/Frm.h"
#include "../ErrorMessages.h"

namespace geck {

std::unique_ptr<geck::Frm> geck::FrmReader::read() {
    try {
        // Use format validation
        FormatValidator::validateFrmFile(getBinaryUtils(), _path);
        
        auto& utils = getBinaryUtils();
        spdlog::debug("Reading FRM file: {}", _path.string());

        auto frm = std::make_unique<Frm>(_path);
        
        uint32_t version = utils.readBE32();
        frm->setVersion(version);
        
        uint16_t fps = utils.readBE16();
        frm->setFps(fps);
        
        frm->setActionFrame(utils.readBE16());
        frm->setFramesPerDirection(utils.readBE16());
        
        spdlog::trace("FRM header: version={}, fps={}, frames_per_dir={}", 
                     version, fps, frm->framesPerDirection());

        // Read direction data arrays
        std::array<uint16_t, Frm::DIRECTIONS> shiftX;
        std::array<uint16_t, Frm::DIRECTIONS> shiftY;
        std::array<uint32_t, Frm::DIRECTIONS> dataOffset;
        
        for (unsigned int i = 0; i != Frm::DIRECTIONS; ++i)
            shiftX[i] = utils.readBE16();
        for (unsigned int i = 0; i != Frm::DIRECTIONS; ++i)
            shiftY[i] = utils.readBE16();
        for (unsigned int i = 0; i != Frm::DIRECTIONS; ++i)
            dataOffset[i] = utils.readBE32();

        spdlog::trace("Read FRM direction arrays: {} directions", Frm::DIRECTIONS);

        std::vector<Direction> directions;
        for (unsigned int i = 0; i != Frm::DIRECTIONS; ++i) {
            // Skip duplicate directions (same data offset as previous)
            if (i > 0 && dataOffset[i - 1] == dataOffset[i]) {
                continue;
            }

            Direction direction{};
            direction.setDataOffset(dataOffset[i]);
            direction.setShiftX(shiftX[i]);
            direction.setShiftY(shiftY[i]);

            directions.emplace_back(direction);
            spdlog::trace("Direction {}: offset={}, shiftX={}, shiftY={}", 
                         i, dataOffset[i], shiftX[i], shiftY[i]);
        }

        uint32_t size_of_frame_data = utils.readBE32();
        spdlog::trace("FRM frame data size: {} bytes", size_of_frame_data);

        auto data_start = utils.getPosition().current;
        
        // Validate frame data size
        if (data_start + size_of_frame_data > utils.getPosition().total) {
            throw ParseException(
                ErrorMessages::frmFileError(_path, 
                    "Frame data size (" + std::to_string(size_of_frame_data) + 
                    ") extends beyond file end"), _path);
        }

        // Process each direction's frame data
        for (auto& direction : directions) {
            // Jump to frames data at frames area
            size_t frame_data_offset = data_start + direction.dataOffset();
            utils.setPosition(frame_data_offset);
            
            spdlog::trace("Processing direction at offset {}", frame_data_offset);

            // Read all frames for this direction
            std::vector<Frame> frames;
            for (unsigned i = 0; i != frm->framesPerDirection(); ++i) {
                uint16_t width = utils.readBE16();
                uint16_t height = utils.readBE16();

                Frame frame(width, height);

                // Number of pixels for this frame (should equal width * height)
                uint32_t pixel_count = utils.readBE32();
                if (pixel_count != width * height) {
                    spdlog::warn("FRM frame {}: pixel count mismatch - header says {}, expected {}",
                               i, pixel_count, width * height);
                }

                frame.setOffsetX(utils.readBE16());
                frame.setOffsetY(utils.readBE16());

                // Read pixel data
                size_t pixel_data_size = frame.width() * frame.height();
                // Use stream read directly for pixel data (large binary blob)
                _stream.read(frame.data(), pixel_data_size);

                frames.emplace_back(frame);
                spdlog::trace("Read frame {}: {}x{}, offset=({},{}), {} pixels", 
                            i, width, height, frame.offsetX(), frame.offsetY(), pixel_data_size);
            }
            direction.setFrames(frames);
        }

        frm->setDirections(directions);
        
        spdlog::debug("Successfully read FRM file with {} directions, {} frames per direction", 
                     directions.size(), frm->framesPerDirection());
        return frm;
        
    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException(
            ErrorMessages::frmFileError(_path, "Parse failed: " + std::string(e.what())), _path);
    }
}

} // namespace geck
