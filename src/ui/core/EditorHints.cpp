#include "ui/core/EditorHints.h"

#include <QStringList>

namespace geck {

namespace {

    QString joinHints(const QStringList& parts) {
        // A middle dot (U+00B7) flanked by spaces. Built from QChar so it's encoding-safe regardless
        // of the source-file/locale encoding — a raw "·" via QLatin1String is read as Latin-1 and
        // renders as mojibake ("Â·") in the status bar.
        static const QString separator = QStringLiteral("  ") + QChar(0x00B7) + QStringLiteral("  ");
        return parts.join(separator);
    }

} // namespace

QString hintForContext(EditorMode mode, bool hasSelection) {
    switch (mode) {
        case EditorMode::Select:
            // Only the keys that genuinely act on a selection: Rotate's "R" toolbar
            // shortcut (live whenever not stamping) and Delete/Backspace. With nothing
            // selected neither does anything, so the hint is empty.
            if (hasSelection) {
                return joinHints({ QStringLiteral("R: rotate"),
                    QStringLiteral("Delete: remove") });
            }
            return QString();

        case EditorMode::PlaceTile:
            // A click (or drag) paints; Esc / right-click leaves placement.
            return QStringLiteral("Esc: exit placement");

        case EditorMode::PlaceExitGrid:
            return joinHints({ QStringLiteral("Click: place exit grid"),
                QStringLiteral("Esc: exit") });

        case EditorMode::MarkExits:
            // "Draw edge": Space flips the live segment's side; Shift snaps the live segment
            // to a clean exit-grid angle; Enter or a double-click finishes the line; Esc abandons it.
            return joinHints({ QStringLiteral("Space: flip side"),
                QStringLiteral("Shift: snap to angle"),
                QStringLiteral("Enter / double-click: finish"),
                QStringLiteral("Esc: cancel") });

        case EditorMode::SetPlayerPosition:
            return joinHints({ QStringLiteral("Click: set player start"),
                QStringLiteral("Esc: cancel") });

        case EditorMode::StampPattern:
            // R cycles the prefab's orientation variants (the Rotate shortcut is disabled
            // while stamping so the key reaches the viewport); Esc cancels.
            return joinHints({ QStringLiteral("R: cycle variant"),
                QStringLiteral("Esc: cancel") });
    }

    return QString();
}

} // namespace geck
