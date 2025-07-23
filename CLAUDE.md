# Claude Development Notes

This file contains important notes and context for Claude to maintain consistency across sessions.

## Map Format References

### Primary Reference
- **URL**: https://falloutmods.fandom.com/wiki/MAP_File_Format
- **Description**: Comprehensive documentation of Fallout 2 MAP file format

### Key Map Structure Information

#### Coordinate Systems
**IMPORTANT**: There are two different coordinate systems in Fallout 2 maps:

1. **Tile Coordinates** (Floor/Roof tiles):
   - Grid size: 100×100 = 10,000 tiles per elevation
   - Constant: `Map::TILES_PER_ELEVATION = 10000`
   - Used for: Floor tiles, roof tiles

2. **Hex Coordinates** (Objects, movement):
   - Grid size: 200×200 = 40,000 hexes total
   - Constants: `HexagonGrid::GRID_WIDTH = 200`, `HexagonGrid::GRID_HEIGHT = 200`
   - Total hexes: `GRID_WIDTH × GRID_HEIGHT = 40,000`
   - Used for: Object placement, character movement, hex-based interactions

#### Common Mistake
- **Never validate hex positions against `TILES_PER_ELEVATION`** - this is wrong!
- Hex positions can be 0-39,999 (valid range)
- Tile positions can be 0-9,999 (valid range)

## Drag and Drop Implementation

### Object Positioning
- Objects are positioned using hex coordinates (0-39,999)
- Use `worldPosToHexPosition()` for hex coordinate conversion
- Use `_hexgrid.getHexByPosition()` to get hex data
- Objects store `position` as hex index in MapObject

### Visual Feedback
- Preview objects should use `setDirection(ObjectDirection(0))` to show single frame
- Apply semi-transparency: `setColor(sf::Color(255, 255, 255, 180))`
- Objects without MapObject need null checks in `Object::setDirection()`

## Build Commands

### Standard Build
```bash
make -j4
```

### Test Commands
```bash
# Run all tests
make test

# Run specific test categories
ctest -L general
ctest -L performance
```

## Common Issues and Solutions

### 1. Sprite Size Issues
- Ensure proper FRM loading using `ResourceManager::texture(frmPath)`
- Call `setDirection()` after `setSprite()` to set correct texture rectangle
- Check for null FRM before calling direction methods

### 2. Coordinate Validation
- Always use correct ranges for validation:
  - Hex positions: `< (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT)`
  - Tile positions: `< Map::TILES_PER_ELEVATION`

### 3. Object Creation Pattern
```cpp
// Standard object creation pattern (existing objects)
auto object = std::make_shared<Object>(frm);
sf::Sprite sprite{ ResourceManager::getInstance().texture(frmPath) };
object->setSprite(std::move(sprite));
object->setDirection(static_cast<ObjectDirection>(direction));
object->setHexPosition(hex);
```

## Code Architecture Notes

### Resource Management
- Use `ResourceManager::getInstance()` for loading assets
- FRM files are stitched into sprite sheets by ResourceManager
- Texture rectangles are set by `Object::setDirection()` to show single frames

### Object Hierarchy
- `MapObject`: Data structure for saving (unique_ptr in map)
- `Object`: Visual representation with SFML sprite (shared_ptr)
- Objects can exist without MapObject for preview purposes

### Event Flow
- Qt drag events → SFMLWidget → EditorWidget
- Coordinate conversion: Qt screen → SFML window → World coordinates
- World coordinates → Hex position via `worldPosToHexPosition()`

## Recent Fixes Applied

### Session Notes
- Fixed hex vs tile coordinate confusion (40,000 vs 10,000 range)
- Implemented proper drag preview with FRM loading
- Added null checks for MapObject access in Object methods
- Corrected validation ranges for hex positioning
- Fixed sprite texture rectangles for single frame display
- **CRITICAL**: Fixed selection crash by using shared_ptr for MapObject instead of unique_ptr

### MapObject Storage Architecture
**IMPORTANT**: All MapObject instances must be `std::shared_ptr<MapObject>`, not `std::unique_ptr`:
- `Map::objects()` returns `std::unordered_map<int, std::vector<std::shared_ptr<MapObject>>>`
- `MapFile::map_objects` also uses `std::vector<std::shared_ptr<MapObject>>`
- `Object::setMapObject()` expects `std::shared_ptr<MapObject>`
- When creating new objects, always use `std::make_shared<MapObject>()`

### Object Creation Pattern (CORRECTED)
```cpp
// Create MapObject as shared_ptr (not unique_ptr!)
auto mapObject = std::make_shared<MapObject>();
// ... set mapObject properties ...

// Add to map storage
mapFile.map_objects[elevation].push_back(mapObject);

// Create visual Object and associate MapObject
auto object = std::make_shared<Object>(frm);
object->setMapObject(mapObject);  // Critical for selection system!
```

---

*Last updated: 2025-01-23*
*This file should be updated whenever significant architectural decisions or fixes are made.*