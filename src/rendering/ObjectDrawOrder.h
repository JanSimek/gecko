#pragma once

#include <algorithm>
#include <ranges>
#include <utility>
#include <vector>

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
 * @brief Visits objects in the engine's draw order: the flat ones, then the standing ones, each
 * pass by ascending hex, and objects on one hex in list order.
 *
 * That is `_obj_render_pre_roof`: each pass walks the hexes in `_orderTable` order, which sorts
 * them by tile number, and a hex's list keeps insertion (map-file) order within flat and standing
 * (`_obj_insert`). Shared by RenderingEngine::renderObjects (what is drawn over what) and
 * ObjectPicker (which object is on top), so a click picks what the user sees. @p mapObjectOf maps
 * an element to its MapObject.
 */
template <typename Range, typename MapObjectOf, typename Visit>
void forEachInDrawOrder(const Range& objects, MapObjectOf mapObjectOf, Visit visit) {
    using Element = std::ranges::range_value_t<Range>;
    std::vector<const Element*> order;
    order.reserve(std::ranges::size(objects));
    for (const auto& object : objects) {
        order.push_back(&object);
    }
    std::ranges::stable_sort(order, {}, [&mapObjectOf](const Element* object) {
        const MapObject& mapObject = mapObjectOf(*object);
        return std::pair{ !isDrawnFlat(mapObject), mapObject.position };
    });
    for (const Element* object : order) {
        visit(*object);
    }
}

} // namespace geck
