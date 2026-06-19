#pragma once

#include <cstdint>

#include "format/map/MapObject.h"

namespace geck {

// MapObject is intentionally a single flat struct that carries every object
// subtype's fields for binary fidelity (it mirrors the on-disk record). These
// lightweight views give subtype-scoped, well-named access to the relevant
// fields without copying or changing that storage: each wraps a MapObject& and
// reads/writes its fields in place. Construct one only for an object of the
// matching subtype (e.g. obj.isExitGridMarker()).

/// Exit-grid destination fields (exit_*).
class ExitGridInstance {
public:
    explicit ExitGridInstance(MapObject& object)
        : _object(object) {
    }

    [[nodiscard]] uint32_t destinationMap() const { return _object.exit_map; }
    void setDestinationMap(uint32_t value) { _object.exit_map = value; }

    [[nodiscard]] uint32_t destinationPosition() const { return _object.exit_position; }
    void setDestinationPosition(uint32_t value) { _object.exit_position = value; }

    [[nodiscard]] uint32_t destinationElevation() const { return _object.exit_elevation; }
    void setDestinationElevation(uint32_t value) { _object.exit_elevation = value; }

    [[nodiscard]] uint32_t orientation() const { return _object.exit_orientation; }
    void setOrientation(uint32_t value) { _object.exit_orientation = value; }

private:
    MapObject& _object;
};

/// Per-instance critter combat/AI fields.
class CritterInstance {
public:
    explicit CritterInstance(MapObject& object)
        : _object(object) {
    }

    [[nodiscard]] uint32_t aiPacket() const { return _object.ai_packet; }
    void setAiPacket(uint32_t value) { _object.ai_packet = value; }

    [[nodiscard]] uint32_t groupId() const { return _object.group_id; }
    void setGroupId(uint32_t value) { _object.group_id = value; }

    [[nodiscard]] uint32_t currentHp() const { return _object.current_hp; }
    void setCurrentHp(uint32_t value) { _object.current_hp = value; }

    [[nodiscard]] uint32_t currentRad() const { return _object.current_rad; }
    void setCurrentRad(uint32_t value) { _object.current_rad = value; }

    [[nodiscard]] uint32_t currentPoison() const { return _object.current_poison; }
    void setCurrentPoison(uint32_t value) { _object.current_poison = value; }

private:
    MapObject& _object;
};

/// Weapon-instance fields: loaded ammo charges and the loaded ammo proto.
class WeaponInstance {
public:
    explicit WeaponInstance(MapObject& object)
        : _object(object) {
    }

    [[nodiscard]] uint32_t ammoCount() const { return _object.ammo; }
    void setAmmoCount(uint32_t value) { _object.ammo = value; }

    [[nodiscard]] uint32_t ammoPid() const { return _object.ammo_pid; }
    void setAmmoPid(uint32_t value) { _object.ammo_pid = value; }

private:
    MapObject& _object;
};

/// Scenery-instance fields: door pass-through and elevator/stairs destination data.
class SceneryInstance {
public:
    explicit SceneryInstance(MapObject& object)
        : _object(object) {
    }

    [[nodiscard]] uint32_t walkThrough() const { return _object.walkthrough; }
    void setWalkThrough(uint32_t value) { _object.walkthrough = value; }

    [[nodiscard]] uint32_t elevationType() const { return _object.elevtype; }
    void setElevationType(uint32_t value) { _object.elevtype = value; }

    [[nodiscard]] uint32_t elevationLevel() const { return _object.elevlevel; }
    void setElevationLevel(uint32_t value) { _object.elevlevel = value; }

    [[nodiscard]] uint32_t destinationElevhex() const { return _object.elevhex; }
    void setDestinationElevhex(uint32_t value) { _object.elevhex = value; }

    [[nodiscard]] uint32_t destinationMap() const { return _object.map; }
    void setDestinationMap(uint32_t value) { _object.map = value; }

private:
    MapObject& _object;
};

} // namespace geck
