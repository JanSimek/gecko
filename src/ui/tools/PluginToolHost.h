#pragma once

namespace geck {

class HexagonGrid;
class Map;
class ObjectCommandController;
class ViewportController;

namespace resource {
    class GameResources;
}

namespace selection {
    class SelectionManager;
}

class PluginToolHost {
public:
    virtual ~PluginToolHost() = default;

    virtual resource::GameResources& resources() const = 0;
    virtual Map* map() const = 0;
    virtual int currentElevation() const = 0;
    virtual const HexagonGrid& hexgrid() const = 0;
    virtual ViewportController& viewport() const = 0;
    virtual selection::SelectionManager* selectionManager() const = 0;
    virtual ObjectCommandController& commandController() = 0;
};

} // namespace geck
