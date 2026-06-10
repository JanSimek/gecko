#pragma once

#include <QDialog>

#include <cstdint>

class QCheckBox;

namespace geck {

/// @brief Editor for door/container interaction state: locked and jammed.
///
/// The engine keeps these bits in different fields depending on type
/// (proto_instance.cc objectIsLocked / objectLock):
///   - doors: obj->data.scenery.door.openFlags  (our MapObject.walkthrough)
///   - containers: obj->data.flags              (our MapObject.unknown11)
/// Both use the same bit values (LOCKED 0x02000000, JAMMED 0x04000000). The
/// dialog edits whichever field applies and returns the other unchanged. No
/// .map format change - both fields already round-trip.
class InstancePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    /// @param isDoor              true for doors, false for containers
    /// @param doorOpenFlags       MapObject.walkthrough (door open/lock flags)
    /// @param containerDataFlags  MapObject.unknown11 (object data flags)
    InstancePropertiesDialog(bool isDoor, uint32_t doorOpenFlags, uint32_t containerDataFlags,
        QWidget* parent = nullptr);

    uint32_t getDoorOpenFlags() const;
    uint32_t getContainerDataFlags() const;

    static constexpr uint32_t FLAG_LOCKED = 0x02000000;
    static constexpr uint32_t FLAG_JAMMED = 0x04000000;

private:
    bool _isDoor;
    uint32_t _doorOpenFlags;
    uint32_t _containerDataFlags;
    QCheckBox* _lockedBox;
    QCheckBox* _jammedBox;
};

} // namespace geck
