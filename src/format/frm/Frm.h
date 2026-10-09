#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "format/IFile.h"
#include "Direction.h"
#include "format/ObjTypes.h"

namespace geck {

class Frm : public IFile {
private:
    uint32_t _version;
    uint16_t _fps;
    uint16_t _actionFrame;
    uint16_t _framesPerDirection;

    std::vector<Direction> _directions;

    uint16_t _maxFrameWidth;
    uint16_t _maxFrameHeight;

    void updateFrameDimensions();

public:
    Frm(const std::filesystem::path& path);

    static constexpr char STANDING_ANIMATION_SUFFIX[] = "aa.frm";

    constexpr static int DIRECTIONS = static_cast<int>(Rotation::Count);

    const std::vector<Direction>& directions() const;
    void setDirections(std::vector<Direction> directions);

    uint32_t version() const;
    void setVersion(const uint32_t& version);

    uint16_t fps() const;
    void setFps(const uint16_t& fps);

    uint16_t actionFrame() const;
    void setActionFrame(const uint16_t& actionFrame);

    uint16_t framesPerDirection() const;
    void setFramesPerDirection(const uint16_t& framesPerDirection);

    uint16_t width() const;
    uint16_t height() const;

    uint16_t maxFrameWidth() const;
    uint16_t maxFrameHeight() const;
};

} // namespace geck
