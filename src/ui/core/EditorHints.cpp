#include "ui/core/EditorHints.h"

#include <QStringList>

#include "ui/input/ActionSpec.h"

namespace geck {

namespace {

    // The key an action is currently on, named the way the status bar should show it. Falls back
    // to the shipped name when no lookup was supplied or the action has been unbound.
    QString keyName(const HintKeyLookup& keyFor, const char* actionId, const QString& fallback) {
        if (!keyFor) {
            return fallback;
        }
        const QString bound = keyFor(QString::fromLatin1(actionId));
        return bound.isEmpty() ? fallback : bound;
    }

    QString joinHints(const QStringList& parts) {
        // A middle dot (U+00B7) flanked by spaces. Built from QChar so it's encoding-safe regardless
        // of the source-file/locale encoding — a raw "·" via QLatin1String is read as Latin-1 and
        // renders as mojibake ("Â·") in the status bar.
        static const QString separator = QStringLiteral("  ") + QChar(0x00B7) + QStringLiteral("  ");
        return parts.join(separator);
    }

} // namespace

QString hintForContext(EditorMode mode, bool hasSelection, const QString& activeToolHint,
    const HintKeyLookup& keyFor) {
    using enum EditorMode;

    switch (mode) {
        case Select:
            // Only the keys that genuinely act on a selection: Rotate's canvas "R" (live
            // whenever not stamping), Enter to inspect it in the Selection panel, and
            // Delete/Backspace. With nothing selected none of them does anything, so the hint
            // is empty.
            if (hasSelection) {
                return joinHints({ keyName(keyFor, actions::PANEL_SELECTION_REVEAL, QStringLiteral("Enter")) + QStringLiteral(": inspect"),
                    keyName(keyFor, actions::TOOL_ROTATE, QStringLiteral("R")) + QStringLiteral(": rotate"),
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
            // R here is the viewport's own stamp key, not the Rotate binding — the Rotate shortcut
            // stands down while stamping precisely so this one reaches InputHandler — so it is
            // written out rather than looked up.
            return joinHints({ QStringLiteral("R: cycle variant"),
                QStringLiteral("Esc: cancel") });

        case PluginTool:
            // A registered tool describes its own keys (ITool::statusHint, one item per
            // line); tools without a hint get the generic cancel line, which the host
            // guarantees (an unconsumed Esc / right-click always leaves the tool).
            if (!activeToolHint.isEmpty()) {
                return joinHints(activeToolHint.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
            }
            return QStringLiteral("Esc / right-click: cancel");
    }

    return QString();
}

} // namespace geck
