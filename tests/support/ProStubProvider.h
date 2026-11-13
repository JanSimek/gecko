#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>

#include "format/pro/Pro.h"
#include "format/pro/ProtoTypes.h"

namespace geck::test {

/// In-memory PRO provider for MapReader/MapWriter. Only ITEM/SCENERY objects
/// dereference their Pro during (de)serialization (for objectSubtypeId());
/// walls/critters/misc do not. A minimal Pro carrying just the subtype is
/// therefore enough, and a whole map round-trip needs no .map or .pro fixture.
struct StubProvider {
    std::map<uint32_t, std::unique_ptr<Pro>> pros;

    void addItem(uint32_t pid, ItemType t) { set(pid, static_cast<unsigned int>(t)); }
    void addScenery(uint32_t pid, SceneryType t) { set(pid, static_cast<unsigned int>(t)); }

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

/// The protos tests/data/sfshutl2.map needs to parse: its objects use ten scenery protos (indices
/// 1538..1547), all of the GENERIC subtype, and no items.
inline StubProvider sfshutl2Provider() {
    StubProvider provider;
    for (uint32_t index = 1538; index <= 1547; ++index) {
        provider.addScenery(ProtoId(ObjectType::Scenery, index).pid(), SceneryType::Generic);
    }
    return provider;
}

} // namespace geck::test
