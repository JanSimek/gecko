#pragma once

#include "ui/core/EditorSession.h"
#include "ui/core/ObjectPicker.h"
#include "ui/core/SelectionVisualizer.h"

namespace geck {

/**
 * @brief Qt-free owner of the editor's per-map state and the helpers that act on it.
 *
 * First seam of the EditorWidget/EditorController split (#1 C–F): the editor's
 * non-Qt core — the per-map EditorSession plus the ObjectPicker (hit-testing) and
 * SelectionVisualizer (selection highlight state) that read it — lives here, while
 * EditorWidget remains the QWidget shell that owns the Qt managers, rendering and
 * input. More logic can migrate in over subsequent stages.
 */
class EditorController {
public:
    EditorController() = default;

    EditorSession& session() { return _session; }
    const EditorSession& session() const { return _session; }

    ObjectPicker& picker() { return _picker; }
    const ObjectPicker& picker() const { return _picker; }
    SelectionVisualizer& visualizer() { return _visualizer; }
    const SelectionVisualizer& visualizer() const { return _visualizer; }

private:
    EditorSession _session;
    ObjectPicker _picker{ _session };
    SelectionVisualizer _visualizer{ _session };
};

} // namespace geck
