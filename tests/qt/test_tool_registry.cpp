#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "ui/tools/ToolRegistry.h"

using namespace geck;

namespace {

class CountingTool : public ITool {
public:
    explicit CountingTool(std::string id, int* activationsOut = nullptr, int* deactivationsOut = nullptr)
        : _id(std::move(id))
        , _activationsOut(activationsOut)
        , _deactivationsOut(deactivationsOut) {
    }

    std::string_view id() const override { return _id; }
    void onActivate() override {
        ++_activations;
        if (_activationsOut) {
            ++*_activationsOut;
        }
    }
    void onDeactivate() override {
        ++_deactivations;
        if (_deactivationsOut) {
            ++*_deactivationsOut;
        }
    }

    int activations() const { return _activations; }
    int deactivations() const { return _deactivations; }

private:
    std::string _id;
    int _activations = 0;
    int _deactivations = 0;
    int* _activationsOut = nullptr;
    int* _deactivationsOut = nullptr;
};

} // namespace

TEST_CASE("ToolRegistry activates one tool at a time", "[tools]") {
    ToolRegistry registry;
    auto first = std::make_unique<CountingTool>("first");
    auto second = std::make_unique<CountingTool>("second");
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();

    REQUIRE(registry.registerTool(std::move(first)));
    REQUIRE(registry.registerTool(std::move(second)));

    REQUIRE(registry.setActiveTool("first"));
    CHECK(registry.activeTool() == firstPtr);
    CHECK(firstPtr->activations() == 1);
    CHECK(firstPtr->deactivations() == 0);

    REQUIRE(registry.setActiveTool("second"));
    CHECK(registry.activeTool() == secondPtr);
    CHECK(firstPtr->deactivations() == 1);
    CHECK(secondPtr->activations() == 1);

    registry.clearActiveTool();
    CHECK(registry.activeTool() == nullptr);
    CHECK(secondPtr->deactivations() == 1);
}

TEST_CASE("ToolRegistry rejects duplicate ids and unregisters active tools", "[tools]") {
    ToolRegistry registry;
    int deactivations = 0;
    auto tool = std::make_unique<CountingTool>("tool", nullptr, &deactivations);

    REQUIRE(registry.registerTool(std::move(tool)));
    CHECK_FALSE(registry.registerTool(std::make_unique<CountingTool>("tool")));

    REQUIRE(registry.setActiveTool("tool"));
    REQUIRE(registry.unregisterTool("tool"));
    CHECK(deactivations == 1);
    CHECK(registry.activeTool() == nullptr);
    CHECK_FALSE(registry.setActiveTool("tool"));
}

TEST_CASE("ToolRegistry exposes sorted tool ids", "[tools]") {
    ToolRegistry registry;
    REQUIRE(registry.registerTool(std::make_unique<CountingTool>("zeta")));
    REQUIRE(registry.registerTool(std::make_unique<CountingTool>("alpha")));
    REQUIRE(registry.registerTool(std::make_unique<CountingTool>("middle")));

    CHECK(registry.toolIds() == std::vector<std::string>{ "alpha", "middle", "zeta" });
}

TEST_CASE("ToolRegistry re-activating the active tool is an idempotent no-op", "[tools]") {
    // Documented contract: stateful tools cannot rely on onActivate re-firing when the user
    // re-invokes the already-active tool; they reset via onDeactivate or their own events.
    ToolRegistry registry;
    auto tool = std::make_unique<CountingTool>("tool");
    auto* toolPtr = tool.get();
    REQUIRE(registry.registerTool(std::move(tool)));

    REQUIRE(registry.setActiveTool("tool"));
    REQUIRE(registry.setActiveTool("tool"));
    CHECK(toolPtr->activations() == 1);
    CHECK(toolPtr->deactivations() == 0);
}
