#pragma once

#include "format/city/CityTxt.h"
#include "format/worldmap/WorldmapTxt.h"
#include "util/PaletteBlend.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace geck::resource {
class GameResources;
}

/// @file
/// @brief The Fallout 2 worldmap rendered the way the engine draws it, as plain pixel buffers.
///
/// The engine composes its worldmap from a grid of 350x300 background tiles (`art/intrface/wrldmp*`,
/// which already carry the yellow subtile grid) with a translucent green circle blended over every
/// known area. It does all of that in palette-index space, so this class does too: it rasterises
/// the tiles into an index canvas, blends the markers into a copy of it through the engine's own
/// blend tables, and only then expands to RGBA. The result is pixel-identical to the game rather
/// than an approximation of it.
///
/// Qt-free by design — the editor wraps the RGBA buffer in a QImage, and headless callers can write
/// it straight out.
///
/// @see fallout2-ce `worldmap.cc` (`wmInterfaceRefresh`, `wmInterfaceDrawCircleOverlay`)

namespace geck::worldmap {

/// One worldmap area as the view needs it: where its marker sits, what to label it, and enough
/// context to answer "what is this?" without going back to the raw city.txt.
struct AreaMarker {
    int index = -1;          ///< city.txt `[Area NN]`
    std::string name;        ///< city.txt area_name (internal)
    std::string displayName; ///< map.msg[1500 + index] (what the game labels it), may be empty
    CityAreaSize size = CityAreaSize::Large;
    int x = 0; ///< marker sprite's left edge, in worldmap pixels
    int y = 0; ///< marker sprite's top edge, in worldmap pixels
    int width = 0;
    int height = 0;
    bool knownAtStart = false;
    bool locked = false;
    std::string terrain;               ///< the terrain under the marker, "" if the grid is absent
    std::vector<std::string> mapFiles; ///< .map files this area's entrances lead to

    /// The label the game shows, falling back to the internal name when map.msg has nothing.
    [[nodiscard]] const std::string& label() const { return displayName.empty() ? name : displayName; }

    [[nodiscard]] bool contains(int px, int py) const {
        return px >= x && py >= y && px <= x + width && py <= y + height;
    }
};

/// The rasterised worldmap plus its areas.
///
/// Construct with @ref load; it returns nullptr when the mounted data has no usable worldmap (no
/// `city.txt`, or a `worldmap.txt` without the `[Tile NN]` art the background is made of).
class WorldMapScene {
public:
    /// Reads city.txt + worldmap.txt from the mounted data, resolves and rasterises the tile art,
    /// and blends the area markers. Returns nullptr if the data isn't there.
    [[nodiscard]] static std::unique_ptr<WorldMapScene> load(resource::GameResources& resources);

    // The cached blend table refers to the tables it was built from, so a scene stays put once
    // constructed. Callers hold it through the unique_ptr load() hands back.
    WorldMapScene(const WorldMapScene&) = delete;
    WorldMapScene& operator=(const WorldMapScene&) = delete;
    WorldMapScene(WorldMapScene&&) = delete;
    WorldMapScene& operator=(WorldMapScene&&) = delete;
    ~WorldMapScene() = default;

    [[nodiscard]] int width() const { return _width; }
    [[nodiscard]] int height() const { return _height; }

    /// The composed image, 4 bytes per pixel in RGBA order, `width() * height()` pixels.
    [[nodiscard]] const std::vector<std::uint8_t>& pixels() const { return _pixels; }

    [[nodiscard]] const std::vector<AreaMarker>& areas() const { return _areas; }

    /// The area whose marker covers a worldmap pixel, or nullptr. Matches the engine's rectangular
    /// hit test (`wmMatchWorldPosToArea`): the lowest-numbered area wins where markers overlap.
    [[nodiscard]] const AreaMarker* areaAt(int px, int py) const;

    /// Redraws with markers shown or hidden. Only the marker rectangles are recomputed, so this is
    /// cheap enough to drive a toggle.
    void setMarkersVisible(bool visible);
    [[nodiscard]] bool markersVisible() const { return _markersVisible; }

    /// The engine's `COLOR_GREEN` as RGB, for drawing labels in the same green the circles use.
    [[nodiscard]] std::uint32_t labelColor() const { return _labelColor; }

    /// The terrain name at a worldmap pixel, or "" when the subtile grid isn't loaded.
    [[nodiscard]] std::string terrainAt(int px, int py) const;

    /// Tile art that could not be resolved or loaded, as `[Tile NN] art_idx=N` descriptions. Empty
    /// on a clean load; a caller should surface these rather than silently show black tiles.
    [[nodiscard]] const std::vector<std::string>& missingArt() const { return _missingArt; }

    [[nodiscard]] const WorldmapTxt& worldmapTxt() const { return _world; }

private:
    WorldMapScene() = default;

    bool rasteriseTiles(resource::GameResources& resources);
    void buildAreas(resource::GameResources& resources);
    void composeMarkers();
    void expandToPixels();

    /// Blends one marker sprite into @p _indices at its position, exactly as
    /// `_dark_translucent_trans_buf_to_buf` does. Returns false when the sprite is unavailable.
    bool blendMarker(const AreaMarker& area);

    CityTxt _city;
    WorldmapTxt _world;

    int _width = 0;
    int _height = 0;

    std::vector<std::uint8_t> _terrainIndices; ///< the tile background alone, never mutated after load
    std::vector<std::uint8_t> _indices;        ///< the working canvas: background + blended markers
    std::vector<std::uint8_t> _pixels;         ///< _indices expanded to RGBA

    /// The three marker sprites (`wrldspr0/1/2.frm`), as raw palette indices with their sizes.
    struct Sprite {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> indices;
        [[nodiscard]] bool valid() const { return width > 0 && height > 0; }
    };
    std::array<Sprite, 3> _sprites;

    std::vector<AreaMarker> _areas;
    std::vector<std::string> _missingArt;

    std::optional<palette::BlendTables> _tables;
    std::optional<palette::BlendTable> _greenBlend;
    std::uint32_t _labelColor = 0x00FF00;
    bool _markersVisible = true;
};

} // namespace geck::worldmap
