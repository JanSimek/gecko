#pragma once

#include "VisibilitySettings.h"
#include "util/UndoStack.h"

namespace geck {

/**
 * @brief Mutable editing state for one open map.
 *
 * Holds the per-session state that the editor mutates while a map is open: the
 * undo history and the layer-visibility settings today, with the document data
 * (map, objects, tiles, elevation, selection) migrating here incrementally as
 * part of the EditorWidget/MainWindow orchestration split.
 *
 * EditorSession is Qt-free: EditorWidget owns one and hands references to it (or
 * to the data it holds) to the rendering, selection, and command collaborators.
 */
class EditorSession final {
public:
    [[nodiscard]] VisibilitySettings& visibility() { return _visibility; }
    [[nodiscard]] const VisibilitySettings& visibility() const { return _visibility; }

    [[nodiscard]] UndoStack& undoStack() { return _undoStack; }
    [[nodiscard]] const UndoStack& undoStack() const { return _undoStack; }

private:
    VisibilitySettings _visibility;
    UndoStack _undoStack{ 100 };
};

} // namespace geck
