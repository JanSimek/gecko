#include "editor/worldmap/WorldMapScene.h"

#include "format/frm/Frame.h"
#include "format/frm/Frm.h"
#include "format/pal/Pal.h"
#include "reader/city/CityTxtReader.h"
#include "reader/worldmap/WorldmapTxtReader.h"
#include "resource/ConfigLoad.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <sstream>

namespace geck::worldmap {

namespace {

    // The engine's OBJ_TYPE_INTERFACE. Both the worldmap tiles (worldmap.txt art_idx) and the city
    // circles are interface art, so their LST indices become FIDs with this type byte.
    constexpr std::uint32_t INTERFACE_FID_TYPE = 6;

    // intrface.lst indices of the small/medium/large city circles: fallout2-ce worldmap.cc builds
    // them as buildFid(OBJ_TYPE_INTERFACE, 336 + citySize).
    constexpr int CITY_SPRITE_BASE_INDEX = 336;

    constexpr std::uint32_t interfaceFid(int lstIndex) {
        return (INTERFACE_FID_TYPE << 24) | static_cast<std::uint32_t>(lstIndex);
    }

    // The first frame of an FRM as raw palette indices. Every image the worldmap uses is a single
    // static frame, so there is no direction or animation to pick.
    const Frame* firstFrame(resource::GameResources& resources, const std::string& artPath) {
        const Frm* frm = resources.repository().load<Frm>(artPath);
        if (frm == nullptr || frm->directions().empty()) {
            return nullptr;
        }
        const auto& frames = frm->directions()[0].frames();
        return frames.empty() ? nullptr : &frames[0];
    }

} // namespace

std::unique_ptr<WorldMapScene> WorldMapScene::load(resource::GameResources& resources) {
    auto scene = std::unique_ptr<WorldMapScene>(new WorldMapScene());
    scene->_city = resource::loadConfig<CityTxt>(resources, { "data/city.txt", "city.txt" },
        [](const std::string& text) { return parseCityTxt(text); });
    if (scene->_city.areas.empty()) {
        spdlog::warn("WorldMapScene: no city.txt with [Area NN] sections in the mounted data");
        return nullptr;
    }

    scene->_world = resource::loadConfig<WorldmapTxt>(resources, { "data/worldmap.txt", "worldmap.txt" },
        [](const std::string& text) { return parseWorldmapTxt(text); });

    const Pal* pal = resources.repository().load<Pal>("color.pal");
    if (pal == nullptr) {
        spdlog::warn("WorldMapScene: color.pal is not mounted; cannot draw the worldmap");
        return nullptr;
    }
    scene->_tables.emplace(*pal);
    scene->_greenBlend.emplace(*scene->_tables, palette::GREEN_RGB);
    scene->_labelColor = scene->_tables->toRgb888(scene->_tables->fromRgb555(palette::rgb555(palette::GREEN_RGB)));

    if (!scene->rasteriseTiles(resources)) {
        return nullptr;
    }
    scene->loadMarkerSprites(resources);
    scene->buildAreas(resources);
    scene->composeMarkers();
    return scene;
}

bool WorldMapScene::rasteriseTiles(resource::GameResources& resources) {
    _width = _world.widthPixels();
    _height = _world.heightPixels();
    if (_width <= 0 || _height <= 0) {
        spdlog::warn("WorldMapScene: worldmap.txt has no tile grid ([Tile Data] num_horizontal_tiles)");
        return false;
    }

    // Unwritten pixels stay index 0, which the palette maps to black — the same thing the engine
    // shows for a tile whose art it cannot grab.
    _terrainIndices.assign(static_cast<std::size_t>(_width) * _height, 0);

    const int columns = _world.numHorizontalTiles;
    for (std::size_t tile = 0; tile < _world.tiles.size(); ++tile) {
        const int artIndex = _world.tiles[tile].artIndex;
        const int originX = static_cast<int>(tile % columns) * WM_TILE_WIDTH;
        const int originY = static_cast<int>(tile / columns) * WM_TILE_HEIGHT;
        if (originY >= _height) {
            break; // a partial trailing row the engine would not show either
        }

        const std::string artPath = artIndex >= 0 ? resources.frmResolver().resolve(interfaceFid(artIndex)) : std::string();
        const Frame* frame = artPath.empty() ? nullptr : firstFrame(resources, artPath);
        if (frame == nullptr) {
            std::ostringstream missing;
            missing << "[Tile " << tile << "] art_idx=" << artIndex;
            if (!artPath.empty()) {
                missing << " (" << artPath << ")";
            }
            _missingArt.push_back(missing.str());
            continue;
        }

        const int copyWidth = std::min<int>(frame->width(), _width - originX);
        const int copyHeight = std::min<int>(frame->height(), _height - originY);
        for (int y = 0; y < copyHeight; ++y) {
            std::uint8_t* row = &_terrainIndices[static_cast<std::size_t>(originY + y) * _width + originX];
            for (int x = 0; x < copyWidth; ++x) {
                row[x] = frame->index(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
            }
        }
    }

    if (!_missingArt.empty()) {
        spdlog::warn("WorldMapScene: {} worldmap tile(s) have no usable art", _missingArt.size());
    }

    return true;
}

void WorldMapScene::loadMarkerSprites(resource::GameResources& resources) {
    // The city circles are interface art too; load them once for every area to blend against.
    for (std::size_t size = 0; size < _sprites.size(); ++size) {
        const int lstIndex = CITY_SPRITE_BASE_INDEX + static_cast<int>(size);
        const std::string path = resources.frmResolver().resolve(interfaceFid(lstIndex));
        const Frame* frame = path.empty() ? nullptr : firstFrame(resources, path);
        if (frame == nullptr) {
            spdlog::warn("WorldMapScene: city marker art {} is missing", lstIndex);
            continue;
        }

        Sprite& sprite = _sprites[size];
        sprite.width = frame->width();
        sprite.height = frame->height();
        sprite.indices.resize(static_cast<std::size_t>(sprite.width) * sprite.height);
        for (int y = 0; y < sprite.height; ++y) {
            for (int x = 0; x < sprite.width; ++x) {
                sprite.indices[static_cast<std::size_t>(y) * sprite.width + x]
                    = frame->index(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
            }
        }
    }
}

void WorldMapScene::buildAreas(resource::GameResources& resources) {
    const resource::MapNameResolver names(resources);

    _areas.reserve(_city.areas.size());
    for (const CityArea& area : _city.areas) {
        AreaMarker marker;
        marker.index = area.index;
        marker.name = area.name;
        marker.displayName = names.areaName(area.index);
        marker.size = cityAreaSize(area.size);
        marker.knownAtStart = area.startOn;
        marker.locked = area.locked;

        // world_pos is measured from the interface window's origin, so the marker's true place on
        // the worldmap is that minus where the view sits inside the window.
        marker.x = area.worldX - WM_VIEW_X;
        marker.y = area.worldY - WM_VIEW_Y;
        const Sprite& sprite = spriteFor(marker.size);
        marker.width = sprite.width;
        marker.height = sprite.height;

        // The engine only ever samples terrain under the *party*, never under an area, so take the
        // middle of the circle: standing anywhere inside it counts as being at this area.
        marker.terrain = _world.terrainAt(marker.x + marker.width / 2, marker.y + marker.height / 2);
        for (const CityEntrance& entrance : area.entrances) {
            const std::string mapFile = names.fileNameOfLookup(entrance.map);
            if (!mapFile.empty()) {
                marker.mapFiles.push_back(mapFile);
            }
        }

        _areas.push_back(std::move(marker));
    }
}

bool WorldMapScene::blendMarker(const AreaMarker& area) {
    const Sprite& sprite = spriteFor(area.size);
    if (!sprite.valid() || !_greenBlend.has_value()) {
        return false;
    }

    // Clip to the canvas; the engine clips to its 450x443 viewport for the same reason. Indices are
    // computed per pixel rather than by offsetting a row pointer by area.x, which would be out of
    // bounds (and so undefined) for a marker hanging off the left edge.
    const int startX = std::max(0, -area.x);
    const int startY = std::max(0, -area.y);
    const int endX = std::min(sprite.width, _width - area.x);
    const int endY = std::min(sprite.height, _height - area.y);

    for (int y = startY; y < endY; ++y) {
        const std::uint8_t* src = &sprite.indices[static_cast<std::size_t>(y) * sprite.width];
        const std::size_t destRow = static_cast<std::size_t>(area.y + y) * _width;
        for (int x = startX; x < endX; ++x) {
            if (src[x] == 0) {
                continue; // index 0 is transparent
            }
            std::uint8_t& dest = _indices[destRow + static_cast<std::size_t>(area.x + x)];
            dest = _greenBlend->blendPixel(src[x], dest);
        }
    }
    return true;
}

void WorldMapScene::composeMarkers() {
    _indices = _terrainIndices;
    if (_markersVisible) {
        // In area order, so overlapping circles stack the way the engine stacks them.
        for (const AreaMarker& area : _areas) {
            blendMarker(area);
        }
    }
    expandToPixels();
}

void WorldMapScene::expandToPixels() {
    _pixels.resize(_indices.size() * 4);
    for (std::size_t i = 0; i < _indices.size(); ++i) {
        const std::uint32_t rgb = _tables->toRgb888(_indices[i]);
        _pixels[i * 4] = static_cast<std::uint8_t>(rgb >> 16);
        _pixels[i * 4 + 1] = static_cast<std::uint8_t>(rgb >> 8);
        _pixels[i * 4 + 2] = static_cast<std::uint8_t>(rgb);
        _pixels[i * 4 + 3] = 0xFF;
    }
}

void WorldMapScene::setMarkersVisible(bool visible) {
    if (_markersVisible == visible) {
        return;
    }
    _markersVisible = visible;
    composeMarkers();
}

const AreaMarker* WorldMapScene::areaAt(int px, int py) const {
    for (const AreaMarker& area : _areas) {
        if (area.contains(px, py)) {
            return &area;
        }
    }
    return nullptr;
}

std::string WorldMapScene::terrainAt(int px, int py) const {
    return _world.terrainAt(px, py);
}

} // namespace geck::worldmap
