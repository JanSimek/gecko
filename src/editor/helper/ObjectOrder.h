#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace geck {

struct MapObject;

/// Draw-order edits for the right-click "Bring to Front / Send to Back" commands.
///
/// gecko draws an elevation's objects in their map-file order, so the order of
/// Map::map_objects[elevation] is the stacking order on screen (last = on top). The engine
/// draws hex by hex instead (fallout2-ce object.cc, _obj_render_pre_roof: flat objects in
/// ascending hex order, then everything else in ascending hex order), and the file order only
/// decides which of several objects on the SAME hex is drawn last. So these edits always change
/// what gecko shows, but in game only between objects sharing a hex.
namespace object_order {

    enum class Step {
        BringToFront,
        BringForward,
        SendBackward,
        SendToBack,
    };

    /// True when two objects' drawn images cover each other. Forward/Backward hop over the
    /// nearest object this accepts, so a step is always one visible change; without a
    /// predicate they hop over the nearest unselected object.
    using Overlaps = std::function<bool(const MapObject& a, const MapObject& b)>;

    /// The elevation's object list after moving `selected` one step. Selected objects keep
    /// their order relative to each other; everything unselected keeps its order. An object
    /// with nothing to hop over stays where it is.
    std::vector<std::shared_ptr<MapObject>> reordered(
        const std::vector<std::shared_ptr<MapObject>>& order,
        const std::vector<std::shared_ptr<MapObject>>& selected,
        Step step,
        const Overlaps& overlaps = {});

} // namespace object_order

} // namespace geck
