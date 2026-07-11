#pragma once

#include <QString>

#include "ui/core/EditorMode.h"

namespace geck {

// Contextual key-hint line for the status bar. Pure and headless (no GL, no widget
// state) so it is unit-testable: given the active tool mode and whether anything is
// selected, it returns the keys that genuinely act in that context, joined by a
// consistent separator. The keys are kept in lockstep with InputHandler's handlers
// and the toolbar shortcuts (e.g. Rotate's "R") that reach the viewport.
//
// For EditorMode::PluginTool, `activeToolHint` carries the active tool's own hint items
// (ITool::statusHint(), one per line); when non-empty they are joined with the standard
// separator, otherwise the generic PluginTool hint is returned.
QString hintForContext(EditorMode mode, bool hasSelection, const QString& activeToolHint = {});

} // namespace geck
