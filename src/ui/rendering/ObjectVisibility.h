#pragma once

#include "format/map/MapObject.h"

namespace geck {

/**
 * @brief Whether an object is currently drawn — and therefore selectable.
 *
 * Selection must only ever return objects the user can actually see, otherwise a click can
 * land on (e.g.) a scroll blocker whose layer is hidden and produce an invisible selection.
 * This is the single rule shared by RenderingEngine::renderObjects (what gets drawn) and
 * EditorWidget::getObjectsAtPosition (what gets picked), so the two cannot drift apart.
 *
 * Templated on the visibility-settings type because the editor and the rendering engine each
 * carry their own struct; both expose the same showObjects/showWalls/showScrollBlockers flags.
 */
template <typename VisibilitySettingsT>
bool isObjectVisible(const MapObject& object, const VisibilitySettingsT& visibility) {
    if (!visibility.showObjects) {
        return false;
    }
    if (!visibility.showWalls && object.isWallObject()) {
        return false;
    }
    if (!visibility.showScrollBlockers && object.isScrollBlocker()) {
        return false;
    }
    return true;
}

} // namespace geck
