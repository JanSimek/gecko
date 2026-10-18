#pragma once

#include <SFML/Graphics.hpp>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace geck {

class HexagonGrid;
class Map;
class Frm;
class Object;
struct MapObject;
class Tile;

namespace resource {
    class GameResources;
}

class MapSpriteLoader {
public:
    struct LoadingErrors {
        size_t objectsSkipped = 0;
        std::set<std::string> failedFrmNames;
        std::vector<std::pair<std::string, int>> failedObjects;

        void clear();
        bool hasErrors() const;
    };

    MapSpriteLoader(resource::GameResources& resources, const HexagonGrid& hexgrid);

    void loadSprites(
        Map& map,
        int elevation,
        std::vector<sf::Sprite>& floorSprites,
        std::vector<sf::Sprite>& roofSprites,
        std::vector<std::shared_ptr<Object>>& objects,
        std::vector<sf::Sprite>& wallBlockerOverlays);
    void loadTileSprites(Map& map, int elevation, std::vector<sf::Sprite>& floorSprites, std::vector<sf::Sprite>& roofSprites) const;
    void loadObjectSprites(Map& map, int elevation, std::vector<std::shared_ptr<Object>>& objects, std::vector<sf::Sprite>& wallBlockerOverlays);
    void appendObjectSprite(
        const std::shared_ptr<MapObject>& mapObject,
        const std::shared_ptr<Object>& object,
        std::vector<std::shared_ptr<Object>>& objects,
        std::vector<sf::Sprite>& wallBlockerOverlays) const;
    void updateTileSprite(
        int hexIndex,
        bool isRoof,
        int elevation,
        const std::vector<Tile>& elevationTiles,
        std::vector<sf::Sprite>& floorSprites,
        std::vector<sf::Sprite>& roofSprites) const;

    const LoadingErrors& lastLoadErrors() const { return _lastLoadErrors; }

private:
    void createWallBlockerOverlay(const std::shared_ptr<MapObject>& mapObject, int hexPosition, std::vector<sf::Sprite>& wallBlockerOverlays) const;
    // A light whose art draws nothing (the game's Light Source scenery is one transparent pixel) is
    // given the light marker art instead, so it can be seen and picked.
    void applyLightMarker(Object& object) const;
    [[nodiscard]] static bool drawsNothing(const Frm& frm);
    [[nodiscard]] bool emitsLight(const MapObject& mapObject) const;
    static const sf::Texture& blankTexture();

    resource::GameResources& _resources;
    const HexagonGrid& _hexgrid;
    LoadingErrors _lastLoadErrors;
};

} // namespace geck
