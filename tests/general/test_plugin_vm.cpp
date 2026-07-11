#include <catch2/catch_test_macros.hpp>

#include <string>

#include "scripting/MapScriptApi.h"
#include "scripting/PluginVm.h"

#include "support/ControllerFixture.h"

using namespace geck;
using geck::test::ControllerFixture;

namespace {
constexpr int ELEV = 0;

PluginVm::Config testConfig(std::string name = "test-plugin") {
    PluginVm::Config config;
    config.name = std::move(name);
    config.dispatchBudgetMs = 100;
    config.maxConsecutiveFaults = 2;
    return config;
}
} // namespace

TEST_CASE("PluginVm globals persist across dispatches", "[scripting][plugin]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
    PluginVm vm(testConfig(), api);
    REQUIRE(vm.start());

    // The persistent env is the point of a resident VM: a chunk's global writes survive into
    // the next dispatch (per-run generation scripts run with READONLY globals instead).
    REQUIRE(vm.dispatch("counter = (counter or 0) + 1 print(counter)"));
    REQUIRE(vm.dispatch("counter = counter + 1 print(counter)"));
    CHECK(vm.console().find("1\n") != std::string::npos);
    CHECK(vm.console().find("2\n") != std::string::npos);
}

TEST_CASE("PluginVm dispatches can read and edit the map through the api", "[scripting][plugin]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
    PluginVm vm(testConfig(), api);
    REQUIRE(vm.start());

    REQUIRE(vm.dispatch("api:paintFloor(0, 271) print(api:getFloor(0))"));
    CHECK(vm.console().find("271") != std::string::npos);
    CHECK(fx.mapFile().tiles.at(ELEV)[0].getFloor() == 271);
}

TEST_CASE("PluginVm survives a script error and auto-disables after repeated faults", "[scripting][plugin]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
    PluginVm vm(testConfig(), api);
    REQUIRE(vm.start());

    // First fault: the VM survives (pcall isolation) and still dispatches.
    CHECK_FALSE(vm.dispatch("error('boom')"));
    CHECK(vm.consecutiveFaults() == 1);
    CHECK_FALSE(vm.disabled());
    REQUIRE(vm.dispatch("print('alive')"));
    CHECK(vm.consecutiveFaults() == 0); // a success resets the streak

    // Two consecutive faults hit the threshold: disabled, and further dispatches are refused.
    CHECK_FALSE(vm.dispatch("error('one')"));
    CHECK_FALSE(vm.dispatch("error('two')"));
    CHECK(vm.disabled());
    CHECK_FALSE(vm.dispatch("print('never runs')"));
    CHECK(vm.console().find("disabled after repeated faults") != std::string::npos);
}

TEST_CASE("PluginVm re-enable brings a fresh state, not the wreckage", "[scripting][plugin]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
    PluginVm vm(testConfig(), api);
    REQUIRE(vm.start());

    REQUIRE(vm.dispatch("counter = 41"));
    CHECK_FALSE(vm.dispatch("error('one')"));
    CHECK_FALSE(vm.dispatch("error('two')"));
    REQUIRE(vm.disabled());

    REQUIRE(vm.enable());
    CHECK_FALSE(vm.disabled());
    // The faulting state's globals are gone by design. The token is distinct from the
    // "re-enabled with a fresh state" log line, so this cannot false-positive on it.
    REQUIRE(vm.dispatch("print(counter == nil and 'env-reset' or 'env-stale')"));
    CHECK(vm.console().find("env-reset") != std::string::npos);
    CHECK(vm.console().find("env-stale") == std::string::npos);
}

TEST_CASE("PluginVm timeout counts as a fault and the VM survives", "[scripting][plugin][watchdog]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
    PluginVm vm(testConfig(), api);
    REQUIRE(vm.start());

    CHECK_FALSE(vm.dispatch("while true do end"));
    CHECK(vm.consecutiveFaults() == 1);
    CHECK(vm.lastError().find("time budget") != std::string::npos);
    REQUIRE(vm.dispatch("print('recovered')"));
}

TEST_CASE("PluginVm memory cap turns runaway allocation into a fault", "[scripting][plugin]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
    PluginVm::Config config = testConfig();
    config.memoryLimitBytes = 512 * 1024;
    config.dispatchBudgetMs = 5000; // let the allocator, not the clock, be what stops it
    PluginVm vm(config, api);
    REQUIRE(vm.start());

    CHECK_FALSE(vm.dispatch("local t = {} for i = 1, 1e8 do t[i] = ('x'):rep(64) end"));
    CHECK(vm.consecutiveFaults() == 1);
    CHECK(vm.lastError().find("memory") != std::string::npos);
    // The state survived the refused allocation and keeps working under the cap.
    REQUIRE(vm.dispatch("print('post-oom')"));
}

TEST_CASE("PluginVm faults cleanly when its api has no open map", "[scripting][plugin]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
    PluginVm vm(testConfig(), api);
    REQUIRE(vm.start());

    // The resident host closed the map: ScriptError surfaces as an ordinary catchable fault.
    api.retarget(fx.resources, fx.hexgrid, fx.controller, nullptr, ELEV, false);
    CHECK_FALSE(vm.dispatch("api:paintFloor(0, 271)"));
    CHECK(vm.lastError().find("no map is open") != std::string::npos);

    // Re-pointing at the map restores the same VM without restarting it.
    api.retarget(fx.resources, fx.hexgrid, fx.controller, fx.map.get(), ELEV, false);
    REQUIRE(vm.dispatch("api:paintFloor(1, 271) print('back')"));
}

TEST_CASE("PluginVm console keeps the newest output under its cap", "[scripting][plugin]") {
    ControllerFixture fx;
    MapScriptApi api(fx.resources, fx.hexgrid, fx.controller, *fx.map, ELEV, false);
    PluginVm::Config config = testConfig();
    config.consoleCapBytes = 256;
    PluginVm vm(config, api);
    REQUIRE(vm.start());

    REQUIRE(vm.dispatch("for i = 1, 100 do print('line-' .. i) end"));
    CHECK(vm.console().size() <= 256);
    CHECK(vm.console().find("line-100") != std::string::npos); // tail kept
    CHECK(vm.console().find("line-1\n") == std::string::npos); // head trimmed
    CHECK(vm.console().front() != '\n');
    CHECK(vm.console().find("line-") == vm.console().find_first_not_of('\n')); // starts at a line
}
