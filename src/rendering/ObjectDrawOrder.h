#pragma once

#include "format/map/MapObject.h"
#include "format/pro/Pro.h"

namespace geck {

/**
 * @brief Whether the engine draws an object in its flat pass, under every standing object.
 *
 * fallout2-ce `_obj_render_pre_roof` draws an elevation's OBJECT_FLAT objects (exit grids, floor
 * markers, ground rubble) before any standing one, whatever the map-file order. The engine reads
 * the instance flags a map stores, not the proto's, so this does too.
 */
inline bool isDrawnFlat(const MapObject& object) {
    return Pro::hasFlag(object.flags, Pro::ObjectFlags::OBJECT_FLAT);
}

/**
 * @brief Visits objects in the order they are drawn: the flat ones first, then the standing ones,
 * each group in list order.
 *
 * Shared by RenderingEngine::renderObjects (what is drawn over what) and ObjectPicker (which object
 * is on top), so a click picks what the user sees. @p mapObjectOf maps an element to its MapObject.
 */
template <typename Range, typename MapObjectOf, typename Visit>
void forEachInDrawOrder(const Range& objects, MapObjectOf mapObjectOf, Visit visit) {
    for (const auto& object : objects) {
        if (isDrawnFlat(mapObjectOf(object))) {
            visit(object);
        }
    }
    for (const auto& object : objects) {
        if (!isDrawnFlat(mapObjectOf(object))) {
            visit(object);
        }
    }
}

} // namespace geck
