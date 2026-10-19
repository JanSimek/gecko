#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "format/map/Map.h"
#include "format/map/MapObject.h"

namespace geck::test {

/// Fills every serialized base field with a distinctive seed-derived value, so a
/// field-order or width bug anywhere in the 22-field base block is caught for
/// each object type. pro_pid / elevation / inventory are set by the caller.
inline void fillBase(MapObject& o, int32_t seed) {
    o.unknown0 = static_cast<uint32_t>(seed * 100 + 1);
    o.position = seed * 100 + 2;
    o.x = static_cast<uint32_t>(seed * 100 + 3);
    o.y = static_cast<uint32_t>(seed * 100 + 4);
    o.sx = -(seed * 100 + 5);
    o.sy = -(seed * 100 + 6);
    o.frame_number = static_cast<uint32_t>(seed * 100 + 7);
    o.direction = static_cast<uint32_t>(seed % 6);
    o.frm_pid = static_cast<uint32_t>(seed * 100 + 9);
    o.flags = static_cast<uint32_t>(seed * 100 + 10);
    o.critter_index = seed * 100 + 11;
    o.light_radius = static_cast<uint32_t>(seed % 9);
    o.light_intensity = static_cast<uint32_t>(seed * 100 + 13);
    o.outline_color = static_cast<uint32_t>(seed % 256);
    o.map_scripts_pid = seed * 100 + 15;
    o.script_id = seed * 100 + 16;
    o.max_inventory_size = static_cast<uint32_t>(seed * 100 + 17);
    o.unknown10 = static_cast<uint32_t>(seed * 100 + 18);
    o.unknown11 = static_cast<uint32_t>(seed * 100 + 19);
}

/// Asserts that every serialized base field of `got` matches `want`.
inline void checkBase(const MapObject& got, const MapObject& want) {
    CHECK(got.unknown0 == want.unknown0);
    CHECK(got.position == want.position);
    CHECK(got.x == want.x);
    CHECK(got.y == want.y);
    CHECK(got.sx == want.sx);
    CHECK(got.sy == want.sy);
    CHECK(got.frame_number == want.frame_number);
    CHECK(got.direction == want.direction);
    CHECK(got.frm_pid == want.frm_pid);
    CHECK(got.flags == want.flags);
    CHECK(got.elevation == want.elevation);
    CHECK(got.pro_pid == want.pro_pid);
    CHECK(got.critter_index == want.critter_index);
    CHECK(got.light_radius == want.light_radius);
    CHECK(got.light_intensity == want.light_intensity);
    CHECK(got.outline_color == want.outline_color);
    CHECK(got.map_scripts_pid == want.map_scripts_pid);
    CHECK(got.script_id == want.script_id);
    CHECK(got.max_inventory_size == want.max_inventory_size);
    CHECK(got.unknown10 == want.unknown10);
    CHECK(got.unknown11 == want.unknown11);
}

/// Appends a bare object (`pid` on `hex`, every other field at its default) to `elevation` of
/// `mapFile`, with `sid` as its map-script id; returns it for further setup.
inline std::shared_ptr<MapObject> addObject(Map::MapFile& mapFile, uint32_t pid, int hex, int elevation = 0,
    int32_t sid = -1) {
    auto object = std::make_shared<MapObject>();
    object->pro_pid = pid;
    object->position = hex;
    object->elevation = static_cast<uint32_t>(elevation);
    object->map_scripts_pid = sid;
    mapFile.map_objects[elevation].push_back(object);
    return object;
}

/// An object of `pid` on `elevation` whose base fields are all fillBase(`seed`) values.
inline std::shared_ptr<MapObject> seededObject(uint32_t pid, int32_t seed, int elevation = 0) {
    auto object = std::make_shared<MapObject>();
    fillBase(*object, seed);
    object->pro_pid = pid;
    object->elevation = static_cast<uint32_t>(elevation);
    return object;
}

/// Appends seededObject(`pid`, `seed`, `elevation`) to `mapFile` and returns it.
inline std::shared_ptr<MapObject> addSeeded(Map::MapFile& mapFile, uint32_t pid, int32_t seed, int elevation = 0) {
    auto object = seededObject(pid, seed, elevation);
    mapFile.map_objects[elevation].push_back(object);
    return object;
}

/// A Map loaded from `filename` holding `mapFile` (an empty map by default). The header keeps its
/// own filename field ("newmap" for an empty map), so code that must name the map by its file
/// cannot pass by reading the header instead.
inline std::unique_ptr<Map> mapOf(const std::string& filename, Map::MapFile mapFile = Map::createEmptyMapFile()) {
    auto map = std::make_unique<Map>(filename);
    map->setMapFile(std::make_unique<Map::MapFile>(std::move(mapFile)));
    return map;
}

/// A Map named "test.map" holding `mapFile` (an empty map by default).
inline std::unique_ptr<Map> mapOf(Map::MapFile mapFile = Map::createEmptyMapFile()) {
    return mapOf("test.map", std::move(mapFile));
}

} // namespace geck::test
