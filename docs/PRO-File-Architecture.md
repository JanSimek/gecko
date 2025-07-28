# PRO File Architecture Analysis

## Overview

This document explains how PRO (Prototype) files are used in the Fallout 2 engine and how they relate to FRM textures and map objects in the GECK Map Editor.

## Core Relationship: PID → PRO → FRM

1. **MapObject.pro_pid** → Points to a PRO file by its Prototype ID
2. **PRO.header.FID** → Contains the Frame ID pointing to the FRM texture
3. **MapObject.frm_pid** → Can override the PRO's FID with a different FRM

## Key Components

### PRO Files as Object Templates

PRO files serve as templates that define object properties and default appearance:

```cpp
// Pro.h structure
struct ProHeader {
    uint32_t PID;        // Prototype ID - unique identifier
    uint32_t textID;     // Text description ID
    uint32_t FID;        // Frame ID - points to FRM file
    // ... other properties like light radius, flags, etc.
};
```

### Map Object FRM Resolution Hierarchy

The engine uses a **map-first approach** for FRM resolution:

```cpp
// From ObjectPalettePanel.cpp - object creation logic
if (mapObject->frm_pid != 0) {
    // Use map-specific FRM override
    frmPath = buildFrmPath(mapObject->frm_pid);
} else {
    // Fall back to PRO file's FID
    frmPath = buildFrmPath(pro->header.FID);
}
```

### Override System

- **MapObject.frm_pid**: When non-zero, overrides the PRO's FID
- **MapObject.pro_pid**: Always points to the PRO for other properties (flags, light, etc.)
- **Use case**: Same object type with different appearance variants

## Practical Implementation

### Object Creation Flow

1. **Palette Selection** → User picks from PRO-based object list
2. **PRO Loading** → System loads PRO file using `pro_pid`
3. **FRM Resolution** → Checks `frm_pid` first, then PRO's `FID`
4. **Texture Loading** → ResourceManager loads the actual FRM texture
5. **Object Rendering** → SFML sprite uses the resolved FRM texture

### Resource Loading Pattern

```cpp
// From EditorWidget.cpp - object placement
auto pro = ProReader::loadPro(mapObject->pro_pid);
std::string frmPath;

if (mapObject->frm_pid != 0) {
    frmPath = buildFrmPath(mapObject->frm_pid);  // Map override
} else {
    frmPath = buildFrmPath(pro->header.FID);     // PRO default
}

auto texture = ResourceManager::getInstance().texture(frmPath);
```

## FRM Texture Priority

Objects take FRM textures using this priority:

1. **First**: MapObject.frm_pid (if non-zero) - **map file override**
2. **Second**: PRO.header.FID - **PRO file default**

## Design Rationale

- **PRO files** define object **behavior and properties** (light radius, flags, blocking)
- **Map files** can **override appearance** while keeping the same object type
- **Example**: A "wooden crate" PRO can appear as different FRM variants (damaged, pristine, etc.) in different map locations

## Current Implementation Status

The map editor correctly implements this system:

- Object palette loads PRO files for object properties
- MapObject creation sets both `pro_pid` and `frm_pid` appropriately  
- Rendering system follows the override hierarchy
- Resource loading handles both PRO defaults and map overrides

## Architecture Benefits

This architecture provides several key benefits:

1. **Flexibility**: Maps can customize object appearance without creating new prototypes
2. **Consistency**: Object behavior remains consistent regardless of visual variant
3. **Memory Efficiency**: Multiple visual variants share the same PRO definition
4. **Modding Support**: Easy to create visual variants without modifying game logic

## Related Documentation

- [MAP File Format](https://falloutmods.fandom.com/wiki/MAP_File_Format)
- [PRO File Format](https://falloutmods.fandom.com/wiki/PRO_File_Format)
- [FRM File Format](https://falloutmods.fandom.com/wiki/FRM_File_Format)

## Code References

Key files for understanding this system:

- `src/format/pro/Pro.h` - PRO file structure definitions
- `src/format/map/MapObject.h` - Map object structure with PID/FID fields
- `src/ui/ObjectPalettePanel.cpp` - Object creation and FRM resolution logic
- `src/ui/EditorWidget.cpp` - Object placement and rendering implementation
- `src/util/ResourceManager.cpp` - FRM texture loading and caching