#pragma once

#include <QDialog>

#include <cstdint>

class QCheckBox;

namespace geck {

/// @brief Editor for door/container locked & jammed state.
///
/// The engine keeps these bits in different fields by type (proto_instance.cc
/// objectIsLocked): doors use openFlags (our MapObject.walkthrough), containers
/// use the object data flags (our MapObject.unknown11). The dialog edits
/// whichever applies and returns the other unchanged.
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

private:
    bool _isDoor;
    uint32_t _doorOpenFlags;
    uint32_t _containerDataFlags;
    QCheckBox* _lockedBox;
    QCheckBox* _jammedBox;
};

} // namespace geck
