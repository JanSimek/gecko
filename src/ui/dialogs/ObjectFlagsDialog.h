#pragma once

#include <QDialog>

#include <cstdint>
#include <vector>

class QCheckBox;

namespace geck {

/// @brief Editor for the per-instance object flags stored in MapObject.flags.
///
/// Exposes the same user-togglable flags as the fallout2-ce mapper instance
/// editor (regModFlagsDialog): Flat, No Block, Multi Hex, the transparency set,
/// Shoot Thru, Light Thru, Wall Trans End, and (items only) No Highlight. Bits
/// that are not shown for the object's type are preserved untouched, so the
/// animation bits and engine-managed flags are never clobbered.
class ObjectFlagsDialog : public QDialog {
    Q_OBJECT

public:
    /// @param flags      current MapObject.flags value
    /// @param objectType Pro::OBJECT_TYPE ordinal (pro_pid >> 24)
    ObjectFlagsDialog(uint32_t flags, uint32_t objectType, QWidget* parent = nullptr);

    /// Returns the edited flags, preserving any bits not shown for this type.
    uint32_t getFlags() const;

private:
    struct Entry {
        QCheckBox* box;
        uint32_t bit;
    };

    void addFlag(class QLayout* layout, const QString& label, uint32_t bit);

    uint32_t _originalFlags;
    uint32_t _shownMask = 0; // union of bits exposed as checkboxes
    std::vector<Entry> _entries;
};

} // namespace geck
