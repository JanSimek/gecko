#pragma once

#include "scripting/MapScriptApi.h"

#include "support/ControllerFixture.h"

namespace geck::test {

/// A MapScriptApi bound to a ControllerFixture's map at elevation 0. `buildSprites` false is the
/// headless, data-only placement path gecko-cli uses; true also builds each placed object's sprite,
/// which needs its art. The api reads no data on construction, so data paths may be mounted after.
struct ScriptApiFixture : ControllerFixture {
    static constexpr int ELEVATION = 0;

    explicit ScriptApiFixture(bool buildSprites = true)
        : api(resources, hexgrid, controller, *map, ELEVATION, buildSprites) { }

    MapScriptApi api;
};

} // namespace geck::test
