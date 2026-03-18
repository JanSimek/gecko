#include "MapSpriteLoader.h"

#include "../../editor/HexagonGrid.h"
#include "../../editor/Object.h"
#include "../../format/frm/Frm.h"
#include "../../format/lst/Lst.h"
#include "../../format/map/Map.h"
#include "../../format/map/MapObject.h"
#include "../../format/map/Tile.h"
#include "../../resource/GameResources.h"
#include "../../util/Constants.h"
#include "../../util/Coordinates.h"
#include "../../util/ResourcePaths.h"
#include "../../util/SpriteFactory.h"
#include "../../util/TileUtils.h"

#include <spdlog/spdlog.h>

namespace geck {

void MapSpriteLoader::LoadingErrors::clear() {
    objectsSkipped = 0;
    failedFrmNames.clear();
    failedObjects.clear();
}

bool MapSpriteLoader::LoadingErrors::hasErrors() const {
    return objectsSkipped > 0;
}

MapSpriteLoader::MapSpriteLoader(resource::GameResources& resources, const HexagonGrid& hexgrid)
    : _resources(resources)
    , _hexgrid(hexgrid) {
}

void MapSpriteLoader::loadSprites(
    Map& map,
    int elevation,
    std::vector<sf::Sprite>& floorSprites,
    std::vector<sf::Sprite>& roofSprites,
    std::vector<std::shared_ptr<Object>>& objects,
    std::vector<sf::Sprite>& wallBlockerOverlays) {
    loadTileSprites(map, elevation, floorSprites, roofSprites);
    loadObjectSprites(map, elevation, objects, wallBlockerOverlays);
}

void MapSpriteLoader::loadTileSprites(
    Map& map,
    int elevation,
    std::vector<sf::Sprite>& floorSprites,
    std::vector<sf::Sprite>& roofSprites) const {
    floorSprites.clear();
    roofSprites.clear();
    floorSprites.reserve(Map::TILES_PER_ELEVATION);
    roofSprites.reserve(Map::TILES_PER_ELEVATION);

    if (map.getMapFile().tiles.find(elevation) == map.getMapFile().tiles.end()) {
        spdlog::warn("MapSpriteLoader::loadTileSprites: Elevation {} does not exist in map data", elevation);
        return;
    }

    for (auto tileNumber = 0U; tileNumber < Map::TILES_PER_ELEVATION; ++tileNumber) {
        const auto& tile = map.getMapFile().tiles.at(elevation).at(tileNumber);
        const auto coords = indexToCoordinates(static_cast<int>(tileNumber));
        const auto screenPos = coordinatesToScreenPosition(coords);

        floorSprites.push_back(SpriteFactory::createFloorTileSprite(_resources, tile.getFloor(), screenPos));
        roofSprites.push_back(SpriteFactory::createRoofTileSprite(_resources, tile.getRoof(), screenPos));
    }
}

void MapSpriteLoader::loadObjectSprites(
    Map& map,
    int elevation,
    std::vector<std::shared_ptr<Object>>& objects,
    std::vector<sf::Sprite>& wallBlockerOverlays) {
    _lastLoadErrors.clear();
    objects.clear();
    wallBlockerOverlays.clear();

    if (map.objects().empty()) {
        return;
    }

    size_t totalObjects = map.objects().at(elevation).size();
    size_t objectsLoaded = 0;
    size_t objectsSkipped = 0;

    spdlog::debug("Loading {} objects for elevation {}", totalObjects, elevation);

    for (const auto& object : map.objects().at(elevation)) {
        if (object->position == -1) {
            continue;
        }

        std::string frmName = _resources.frmResolver().resolve(object->frm_pid);
        if (frmName.empty()) {
            spdlog::error("Empty FRM name for object at position {} (frm_pid=0x{:08X}, pro_pid=0x{:08X})",
                object->position, object->frm_pid, object->pro_pid);
            continue;
        }

        spdlog::debug("Loading object sprite: FRM='{}', position={}, direction={}, frm_pid=0x{:08X}, pro_pid=0x{:08X}",
            frmName, object->position, object->direction, object->frm_pid, object->pro_pid);
        const Frm* frm = _resources.repository().find<Frm>(frmName);

        if (!frm) {
            spdlog::debug("FRM '{}' not in cache, attempting on-demand loading", frmName);
            try {
                frm = _resources.repository().load<Frm>(frmName);
                if (!frm) {
                    spdlog::error("Failed to load FRM resource '{}' for object at position {} - resource still null after loading", frmName, object->position);
                    _lastLoadErrors.failedFrmNames.insert(frmName);
                    _lastLoadErrors.failedObjects.emplace_back(frmName, object->position);
                    objectsSkipped++;
                    continue;
                }
                spdlog::debug("Successfully loaded FRM '{}' on-demand", frmName);
            } catch (const std::exception& e) {
                spdlog::error("Failed to load FRM '{}' for object at position {}: {}", frmName, object->position, e.what());
                _lastLoadErrors.failedFrmNames.insert(frmName);
                _lastLoadErrors.failedObjects.emplace_back(frmName, object->position);
                objectsSkipped++;
                continue;
            }
        }

        spdlog::debug("FRM '{}' available: {} directions, filename='{}'",
            frmName, frm->directions().size(), frm->filename());

        try {
            auto sceneObject = std::make_shared<Object>(frm);
            sf::Sprite objectSprite{ _resources.textures().get(frmName) };
            sceneObject->setSprite(std::move(objectSprite));
            if (auto hex = _hexgrid.getHexByPosition(object->position); hex.has_value()) {
                sceneObject->setHexPosition(hex->get());
            }
            sceneObject->setMapObject(object);
            sceneObject->setDirection(static_cast<ObjectDirection>(object->direction));

            objects.emplace_back(std::move(sceneObject));
            spdlog::debug("Successfully created object for FRM '{}' at position {}", frmName, object->position);
            objectsLoaded++;
        } catch (const std::exception& e) {
            spdlog::error("Failed to create object for FRM '{}' at position {}: {}",
                frmName, object->position, e.what());
            _lastLoadErrors.failedFrmNames.insert(frmName);
            _lastLoadErrors.failedObjects.emplace_back(frmName, object->position);
            objectsSkipped++;
        }
    }

    size_t overlaysCreated = 0;
    for (const auto& object : map.objects().at(elevation)) {
        if (object->position == -1) {
            continue;
        }

        size_t overlayCountBefore = wallBlockerOverlays.size();
        createWallBlockerOverlay(object, object->position, wallBlockerOverlays);
        if (wallBlockerOverlays.size() > overlayCountBefore) {
            overlaysCreated++;
        }
    }

    _lastLoadErrors.objectsSkipped = objectsSkipped;
    spdlog::info("Object loading complete for elevation {}: {} loaded, {} skipped, {} total, {} wall blocker overlays",
        elevation, objectsLoaded, objectsSkipped, totalObjects, overlaysCreated);
}

void MapSpriteLoader::appendObjectSprite(
    const std::shared_ptr<MapObject>& mapObject,
    const std::shared_ptr<Object>& object,
    std::vector<std::shared_ptr<Object>>& objects,
    std::vector<sf::Sprite>& wallBlockerOverlays) const {
    if (!mapObject || !object || mapObject->position == -1) {
        return;
    }

    objects.push_back(object);
    createWallBlockerOverlay(mapObject, mapObject->position, wallBlockerOverlays);
}

void MapSpriteLoader::updateTileSprite(
    int hexIndex,
    bool isRoof,
    int elevation,
    const std::vector<Tile>& elevationTiles,
    std::vector<sf::Sprite>& floorSprites,
    std::vector<sf::Sprite>& roofSprites) const {
    auto tileIndex = _hexgrid.tileIndexForPosition(hexIndex);
    if (!tileIndex.has_value()) {
        return;
    }

    if (*tileIndex >= static_cast<int>(elevationTiles.size())) {
        spdlog::warn("MapSpriteLoader::updateTileSprite: Tile index {} out of bounds (hex {})", *tileIndex, hexIndex);
        return;
    }

    const auto& tile = elevationTiles[*tileIndex];
    int tileId = isRoof ? tile.getRoof() : tile.getFloor();
    auto& sprites = isRoof ? roofSprites : floorSprites;

    if (tileId == Map::EMPTY_TILE) {
        auto screenPos = indexToScreenPosition(*tileIndex, isRoof);
        sprites[*tileIndex].setTexture(blankTexture());
        sprites[*tileIndex].setColor(TileColors::transparent());
        sprites[*tileIndex].setPosition({ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) });
        spdlog::debug("MapSpriteLoader::updateTileSprite: Set tile {} to empty [roof: {}]", *tileIndex, isRoof);
        return;
    }

    try {
        const auto* tileList = _resources.repository().find<Lst>("art/tiles/tiles.lst");
        if (!tileList || tileId >= static_cast<int>(tileList->list().size())) {
            return;
        }

        const std::string tileName = tileList->list()[tileId];
        const std::string tilePath = "art/tiles/" + tileName;
        const auto& texture = _resources.textures().get(tilePath);

        sprites[*tileIndex].setTexture(texture);
        sprites[*tileIndex].setColor(sf::Color::White);

        auto screenPos = indexToScreenPosition(*tileIndex, isRoof);
        sprites[*tileIndex].setPosition({ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) });

        spdlog::debug("MapSpriteLoader::updateTileSprite: Updated sprite for hex {} -> tile {} ({}) [roof: {}], elevation {}",
            hexIndex, *tileIndex, tileName, isRoof, elevation);
    } catch (const std::exception& e) {
        spdlog::warn("MapSpriteLoader::updateTileSprite: Failed to update tile sprite: {}", e.what());
    }
}

void MapSpriteLoader::createWallBlockerOverlay(
    const std::shared_ptr<MapObject>& mapObject,
    int hexPosition,
    std::vector<sf::Sprite>& wallBlockerOverlays) const {
    bool blocks = mapObject->blocksMovement(_resources);

    spdlog::debug("createWallBlockerOverlay: hex {}, pro_pid 0x{:08X}, blocks: {}",
        hexPosition, mapObject->pro_pid, blocks);

    if (!blocks) {
        return;
    }

    bool isShootThrough = mapObject->isShootThroughWallBlocker(_resources);

    try {
        std::string overlayFrmPath = isShootThrough
            ? std::string(ResourcePaths::Frm::WALL_BLOCK)
            : std::string(ResourcePaths::Frm::WALL_BLOCK_FULL);
        _resources.textures().preload(overlayFrmPath);

        sf::Sprite overlaySprite{ _resources.textures().get(overlayFrmPath) };
        auto hex = _hexgrid.getHexByPosition(static_cast<uint32_t>(hexPosition));
        if (!hex.has_value()) {
            return;
        }

        float x = static_cast<float>(hex->get().x() + SpriteOffset::HEX_HIGHLIGHT_X);
        float y = static_cast<float>(hex->get().y() + SpriteOffset::HEX_HIGHLIGHT_Y);
        overlaySprite.setPosition(sf::Vector2f(x, y));
        overlaySprite.setColor(sf::Color(255, 255, 255, OverlayColors::WALL_BLOCKER_ALPHA));

        wallBlockerOverlays.push_back(std::move(overlaySprite));
        spdlog::debug("Created wall blocker overlay for object at hex {} (pro_pid {})", hexPosition, mapObject->pro_pid);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to create wall blocker overlay for object at hex {}: {}", hexPosition, e.what());
    }
}

const sf::Texture& MapSpriteLoader::blankTexture() {
    static sf::Texture* texture = [] {
        auto* t = new sf::Texture();
        sf::Image blankImage{ sf::Vector2u{ 1, 1 }, sf::Color::Transparent };
        [[maybe_unused]] const bool loadSuccess = t->loadFromImage(blankImage);
        return t;
    }();
    return *texture;
}

} // namespace geck
