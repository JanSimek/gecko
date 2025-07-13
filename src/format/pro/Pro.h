#pragma once

#include <istream>
#include <filesystem>

#include "../IFile.h"

namespace geck {

class Pro : public IFile {
private:
    unsigned int _objectSubtypeId;

public:
    Pro(std::filesystem::path path)
        : IFile(path) { }

    enum class OBJECT_TYPE : uint32_t {
        ITEM = 0,
        CRITTER,
        SCENERY,
        WALL,
        TILE,
        MISC
    };

    enum class ITEM_TYPE : uint32_t {
        ARMOR = 0,
        CONTAINER,
        DRUG,
        WEAPON,
        AMMO,
        MISC,
        KEY
    };

    enum class SCENERY_TYPE : uint32_t {
        DOOR = 0,
        STAIRS,
        ELEVATOR,
        LADDER_BOTTOM,
        LADDER_TOP,
        GENERIC
    };

    struct ProHeader {
        int32_t PID;
        uint32_t message_id;
        int32_t FID;
        uint32_t light_distance;
        uint32_t light_intensity;
        uint32_t flags;
    } header;

    unsigned int objectSubtypeId();
    void setObjectSubtypeId(unsigned int objectSubtypeId);

    OBJECT_TYPE type() const;

    const std::string typeToString() const;
};

} // namespace geck
