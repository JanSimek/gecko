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
    using enum EditorMode;

    switch (mode) {
        case Select:
            // Only the keys that genuinely act on a selection: Rotate's "R" toolbar
            // shortcut (live whenever not stamping) and Delete/Backspace. With nothing
            // selected neither does anything, so the hint is empty.
            if (hasSelection) {
                return joinHints({ QStringLiteral("R: rotate"),
                    QStringLiteral("Delete: remove") });
            }
            return QString();

        case PlaceTile:
            // A click (or drag) paints; Esc / right-click leaves placement.
            return QStringLiteral("Esc: exit placement");

        case PlaceExitGrid:
            return joinHints({ QStringLiteral("Click: place exit grid"),
                QStringLiteral("Esc: exit") });

        case MarkExits:
            // "Draw edge" keys: flip side, snap to angle, finish, cancel.
            return joinHints({ QStringLiteral("Space: flip side"),
                QStringLiteral("Shift: snap to angle"),
                QStringLiteral("Enter / double-click: finish"),
                QStringLiteral("Esc: cancel") });

        case SetPlayerPosition:
            return joinHints({ QStringLiteral("Click: set player start"),
                QStringLiteral("Esc: cancel") });

        case StampPattern:
            // R cycles the prefab's orientation variants (the Rotate shortcut is disabled
            // while stamping so the key reaches the viewport); Esc cancels.
            return joinHints({ QStringLiteral("R: cycle variant"),
                QStringLiteral("Esc: cancel") });

        case PlaceObject:
            // The picked object's ghost tracks the cursor; a click drops a copy, R rotates it (the
            // Rotate shortcut is disabled while placing so the key reaches the viewport).
            return joinHints({ QStringLiteral("Click: place"),
                QStringLiteral("R: rotate"),
                QStringLiteral("Esc / right-click: cancel") });

        case PluginTool:
            return QStringLiteral("Esc / right-click: cancel");
    }

    return QString();
}

} // namespace geck
