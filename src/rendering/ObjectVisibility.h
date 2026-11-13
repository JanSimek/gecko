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
 * Each layer toggle controls its own category independently: walls follow showWalls, critters
 * showCritters, scroll blockers showScrollBlockers, and everything else (items, scenery, tiles,
 * misc) the generic showObjects toggle. showObjects is NOT a master switch over the others.
 *
 * Templated on the visibility-settings type because the editor and the rendering engine each
 * carry their own struct; both expose the same show* flags.
 */
template <typename VisibilitySettingsT>
bool isObjectVisible(const MapObject& object, const VisibilitySettingsT& visibility) {
    // Scroll blockers are a MISC proto, so check them before the type-based buckets.
    if (object.isScrollBlocker()) {
        return visibility.showScrollBlockers;
    }
    if (object.isWallObject()) {
        return visibility.showWalls;
    }
    if (object.objectType() == ObjectType::Critter) {
        return visibility.showCritters;
    }
    return visibility.showObjects;
}

} // namespace geck
