#pragma once

#include <SFML/Graphics.hpp>

#include <memory>
#include <vector>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "resource/GameResources.h"
#include "editing/commands/ObjectCommandController.h"
#include "ui/rendering/MapSpriteLoader.h"
#include "util/UndoStack.h"

namespace geck::test {

/// Wires an ObjectCommandController to a fresh empty map for headless tests. The
/// resource/sprite dependencies are default-constructed; the rendering callbacks are
/// no-ops, so commands that only touch the map's objects, tiles and scripts work
/// without a graphics context.
struct ControllerFixture {
    resource::GameResources resources;
    HexagonGrid hexgrid;
    MapSpriteLoader spriteLoader{ resources, hexgrid };
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<sf::Sprite> overlays;
    UndoStack undoStack;
    std::unique_ptr<Map> map;
    ObjectCommandController controller;

    ControllerFixture()
        : map(std::make_unique<Map>("test.map"))
        , controller(
              resources, map, hexgrid, spriteLoader, objects, overlays, undoStack,
              [] { /* refreshObjects: no rendering in tests */ },
              [] { /* onStackChanged: no UI to notify */ },
              [this](int elevation) -> std::vector<Tile>& { return map->getMapFile().tiles[elevation]; },
              [] { return 0; },
              [](int, bool, int) { /* updateTileSprite: no rendering in tests */ },
              [] { /* reloadTiles: no rendering in tests */ }) {
        map->setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));
    }

    Map::MapFile& mapFile() { return map->getMapFile(); }
};

} // namespace geck::test
