#include "ObjectOrder.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace geck::object_order {

namespace {

    using List = std::vector<std::shared_ptr<MapObject>>;
    using Selection = std::unordered_set<const MapObject*>;

    List moveToEnd(const List& order, const Selection& selected, bool toFront) {
        List picked;
        List rest;
        for (const auto& object : order) {
            (selected.contains(object.get()) ? picked : rest).push_back(object);
        }
        List result = toFront ? rest : picked;
        const List& tail = toFront ? picked : rest;
        result.insert(result.end(), tail.begin(), tail.end());
        return result;
    }

    bool hopsOver(const MapObject& moving, const MapObject& other, const Selection& selected, const Overlaps& overlaps) {
        return !selected.contains(&other) && (!overlaps || overlaps(moving, other));
    }

    // Forward: walk the selection from the top down, so an object that moves never hops over a
    // selected object that is still waiting to move (which would flip the selection's own order).
    void stepForward(List& list, const Selection& selected, const Overlaps& overlaps) {
        for (std::size_t i = list.size(); i-- > 0;) {
            if (!selected.contains(list[i].get())) {
                continue;
            }
            for (std::size_t j = i + 1; j < list.size(); ++j) {
                if (selected.contains(list[j].get())) {
                    break; // a selected object above already holds this spot in the stack
                }
                if (hopsOver(*list[i], *list[j], selected, overlaps)) {
                    std::rotate(list.begin() + static_cast<std::ptrdiff_t>(i),
                        list.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                        list.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                    break;
                }
            }
        }
    }

    void stepBackward(List& list, const Selection& selected, const Overlaps& overlaps) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (!selected.contains(list[i].get())) {
                continue;
            }
            for (std::size_t j = i; j-- > 0;) {
                if (selected.contains(list[j].get())) {
                    break;
                }
                if (hopsOver(*list[i], *list[j], selected, overlaps)) {
                    std::rotate(list.begin() + static_cast<std::ptrdiff_t>(j),
                        list.begin() + static_cast<std::ptrdiff_t>(i),
                        list.begin() + static_cast<std::ptrdiff_t>(i) + 1);
                    break;
                }
            }
        }
    }

} // namespace

std::vector<std::shared_ptr<MapObject>> reordered(
    const std::vector<std::shared_ptr<MapObject>>& order,
    const std::vector<std::shared_ptr<MapObject>>& selected,
    Step step,
    const Overlaps& overlaps) {
    Selection picked;
    for (const auto& object : selected) {
        if (object) {
            picked.insert(object.get());
        }
    }
    if (picked.empty()) {
        return order;
    }

    switch (step) {
        case Step::BringToFront:
            return moveToEnd(order, picked, true);
        case Step::SendToBack:
            return moveToEnd(order, picked, false);
        case Step::BringForward: {
            List list = order;
            stepForward(list, picked, overlaps);
            return list;
        }
        case Step::SendBackward: {
            List list = order;
            stepBackward(list, picked, overlaps);
            return list;
        }
    }
    return order;
}

} // namespace geck::object_order
