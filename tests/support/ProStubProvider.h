#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>

#include "format/pro/Pro.h"

namespace geck::test {

/// Builds a PID from an object type (high byte) and a per-type index, matching
/// the engine's `(type << 24) | index` layout. The MapReader/MapWriter and the
/// PRO suites all need this to fabricate typed objects without a .pro on disk.
inline uint32_t pidOf(Pro::OBJECT_TYPE type, uint32_t index) {
    return (static_cast<uint32_t>(type) << 24) | index;
}

/// In-memory PRO provider for MapReader/MapWriter. Only ITEM/SCENERY objects
/// dereference their Pro during (de)serialization (for objectSubtypeId());
/// walls/critters/misc do not. A minimal Pro carrying just the subtype is
/// therefore enough, and a whole map round-trip needs no .map or .pro fixture.
struct StubProvider {
    std::map<uint32_t, std::unique_ptr<Pro>> pros;

    void addItem(uint32_t pid, Pro::ITEM_TYPE t) { set(pid, static_cast<unsigned int>(t)); }
    void addScenery(uint32_t pid, Pro::SCENERY_TYPE t) { set(pid, static_cast<unsigned int>(t)); }

    void set(uint32_t pid, unsigned int subtype) {
        auto pro = std::make_unique<Pro>(std::filesystem::path("stub"));
        pro->setObjectSubtypeId(subtype);
        pros[pid] = std::move(pro);
    }

    Pro* load(uint32_t pid) const {
        auto it = pros.find(pid);
        return it != pros.end() ? it->second.get() : nullptr;
    }
};

} // namespace geck::test
