# FRM File Format and Critter Animation System

## Overview

This document provides comprehensive documentation of the FRM (Frame) file format used in Fallout 2, with particular focus on the complex critter animation system. This information is based on analysis of the legacy F2 Mapper implementation and the Fallout 2 Community Edition source code.

## Table of Contents

1. [FRM File Format Basics](#frm-file-format-basics)
2. [Critter Animation System](#critter-animation-system)
3. [FRM ID (FID) Encoding](#frm-id-fid-encoding)
4. [Animation Types and Suffixes](#animation-types-and-suffixes)
5. [LST File Integration](#lst-file-integration)
6. [Implementation Details](#implementation-details)
7. [Examples](#examples)

## FRM File Format Basics

### What are FRM Files?

FRM files are the primary graphics format used in Fallout 2 for sprites and animations. They contain:
- Multiple directions (up to 6: NE, E, SE, SW, W, NW)
- Multiple frames per direction for animations
- Palette-indexed pixel data
- Offset information for proper positioning

### FRM File Structure

```
Header (62 bytes):
- Version (4 bytes)
- FPS (2 bytes) 
- Action Frame (2 bytes)
- Frame Count per Direction (2 bytes)
- Direction Offsets X (12 bytes, 6 * 2)
- Direction Offsets Y (12 bytes, 6 * 2)
- Direction Data Offsets (24 bytes, 6 * 4)
- Frame Data Size (4 bytes)

Frame Data:
- Per Frame: Width, Height, Size, Offset X, Offset Y, Pixel Data
```

### FRM Types by Directory

| Type ID | Directory | Description |
|---------|-----------|-------------|
| 0 | `art/items/` | Items (weapons, armor, etc.) |
| 1 | `art/critters/` | Characters and creatures |
| 2 | `art/scenery/` | Environmental objects |
| 3 | `art/walls/` | Wall tiles |
| 4 | `art/tiles/` | Floor and roof tiles |
| 5 | `art/misc/` | Miscellaneous objects |
| 6 | `art/intrface/` | Interface elements |
| 7 | `art/inven/` | Inventory item graphics |

## Critter Animation System

### The Challenge

Critter FRM files use a sophisticated naming convention that differs significantly from other object types:
- **LST Entry**: `harobe,11,1` (base name + metadata)
- **Actual Files**: `harobeaa.frm`, `harobeab.frm`, `harobeal.frm`, etc.

The system must map between these naming conventions to properly load and save critter animations.

### Critter File Naming Convention

Critter FRM files follow this pattern:
```
[6-char base name][2-char animation suffix].[extension]
```

**Examples:**
- `harobeaa.frm` - Standing animation (base direction)
- `harobeab.frm` - Walking animation
- `harobeaa.fr0` - Standing animation, direction 0
- `harobeaa.fr1` - Standing animation, direction 1

### Animation Suffix System

The 2-character suffix encodes the animation type:

| Suffix | Animation Type | Description |
|--------|----------------|-------------|
| `aa` | Standing | Default idle animation |
| `ab` | Walking | Movement animation |
| `ad` | Running | Fast movement |
| `ae` | Sneaking | Stealth movement |
| `af` | Single Attack | Basic attack |
| `ag` | Burst Attack | Automatic weapon fire |
| `ah` | Thrust Attack | Melee thrust |
| `ai` | Throw Attack | Throwing animation |
| `aj` | Dodge | Evasion animation |
| `ak` | Damage | Taking damage |
| `al` | Dead (Front) | Death animation (falling forward) |
| `an` | Dead (Back) | Death animation (falling backward) |
| `ao` | Unconscious | Knocked out state |
| `ap` | Pickup | Picking up items |
| `aq` | Use | Using objects |
| `ar` | Climb | Climbing animation |
| `ba` | Weapon Idle | Armed idle pose |
| `bb` | Weapon Walk | Armed movement |
| `bf` | Weapon Single | Armed single attack |
| `bg` | Weapon Burst | Armed burst fire |
| `ch` | Called Shot Head | Targeted head attack |
| `cj` | Called Shot Groin | Targeted groin attack |
| `da` | Magic Hands Begin | Starting hand animation |
| `ra` | Rotation | Turning animation |

### Direction Extensions

FRM files can have different extensions indicating viewing direction:
- `.frm` - Main/default direction (usually NE)
- `.fr0` - Direction 0 (NE)
- `.fr1` - Direction 1 (E)
- `.fr2` - Direction 2 (SE)
- `.fr3` - Direction 3 (SW)
- `.fr4` - Direction 4 (W)
- `.fr5` - Direction 5 (NW)

## FRM ID (FID) Encoding

### FID Structure (32-bit)

```
Bits 31-28: Direction ID (ID3)
Bits 27-24: Object Type (always 1 for critters)
Bits 23-16: Animation Type High (ID2)
Bits 15-12: Animation Type Low (ID1)
Bits 11-0:  Base Critter Index (from LST)
```

### Encoding Process

1. **Extract base index** from LST file position
2. **Parse animation suffixes** to get ID1 and ID2 values
3. **Parse direction** from file extension to get ID3
4. **Combine into FID**: `(1 << 24) | (ID3 << 28) | (ID2 << 16) | (ID1 << 12) | baseIndex`

### Suffix to ID Mapping

The animation suffixes map to ID values through a complex encoding system:

```cpp
// Examples of suffix mappings:
"aa" -> ID1=0, ID2=0    // Standing
"ab" -> ID1=0, ID2=1    // Walking  
"ch" -> ID1=0, ID2=0x24 // Called shot head
"cj" -> ID1=0, ID2=0x25 // Called shot groin
"ba" -> ID1=0, ID2=0x14 // Weapon idle
```

The full mapping follows the legacy mapper's `getSuffixes()` function logic with special cases for various animation types.

## LST File Integration

### Critter LST Format

The `art/critters/critters.lst` file contains entries like:
```
reserv
hapowr,21,1
harobe,11,1
hfcmbt,11,1
```

Each entry contains:
- **Base name** (6 characters): The critter identifier
- **Frame count** (optional): Number of frames per animation
- **Direction count** (optional): Number of directions available

### Resolution Process

1. **Load LST file** to get list of critter bases
2. **Extract base name** from LST entry (part before comma)
3. **Match FRM filename** against base name + animation pattern
4. **Use LST index** as base index for FID encoding

## Implementation Details

### CritterFrmResolver Class

Our implementation provides several key methods:

```cpp
class CritterFrmResolver {
public:
    // Generate FRM filename from FID
    static std::string generateCritterFrmName(const std::string& baseName, uint32_t frmPid);
    
    // Derive FID from filename
    static uint32_t deriveCritterFrmPid(const std::string& baseName, 
                                       const std::string& frmFilename, 
                                       uint32_t baseIndex);
    
    // Get human-readable animation name
    static std::string getAnimationTypeName(const std::string& frmFilename);
    
    // Check if filename matches critter base
    static bool matchesCritterBase(const std::string& baseName, const std::string& frmFilename);
};
```

### Key Functions

**`getSuffixes(ID1, ID2)`**: Converts ID values to animation suffix characters
**`suffixesToIds(suffix1, suffix2)`**: Reverse conversion from suffixes to ID values
**`parseAnimationSuffixes(filename)`**: Extracts suffixes and direction from filename

## Examples

### Example 1: Basic Standing Animation

**LST Entry**: `harobe,11,1` (index 2 in critters.lst)
**FRM File**: `harobeaa.frm`
**Breakdown**:
- Base: `harobe`
- Animation: `aa` (standing, ID1=0, ID2=0)
- Direction: `m` (main direction, ID3=0)
- **FID**: `0x01000002` (type=1, id3=0, id2=0, id1=0, index=2)

### Example 2: Weapon Attack Animation

**FRM File**: `harobebf.fr2`
**Breakdown**:
- Base: `harobe`
- Animation: `bf` (weapon single attack, ID1=0, ID2=0x15)
- Direction: `2` (SE direction, ID3=3)
- **FID**: `0x31150002` (type=1, id3=3, id2=0x15, id1=0, index=2)

### Example 3: Called Shot Animation

**FRM File**: `harobech.frm`
**Breakdown**:
- Base: `harobe`
- Animation: `ch` (called shot head, ID1=0, ID2=0x24)
- Direction: `m` (main direction, ID3=0)
- **FID**: `0x01240002` (type=1, id3=0, id2=0x24, id1=0, index=2)

## Troubleshooting

### Common Issues

1. **"Could not derive FID" errors**
   - Check if base name extraction is correct
   - Verify animation suffix is supported
   - Ensure LST file contains the base name

2. **Incorrect animation playback**
   - Verify FID encoding matches expected values
   - Check direction encoding (fr0-fr5 vs frm)
   - Ensure suffix mapping is correct

3. **Map loading failures**
   - Confirm FID values match original engine expectations
   - Check for proper bit field encoding
   - Verify LST index accuracy

### Debug Information

Enable debug logging to see:
- FID derivation process
- Animation type detection
- Suffix parsing results
- Base name matching

```
[debug] CritterFrmResolver: Found critter FID 0x01000002 (type=1, baseId=2) for: /art/critters/harobeaa.frm (base: harobe, animation: Standing)
```

## References

- [Fallout Wiki: MAP File Format](https://falloutmods.fandom.com/wiki/MAP_File_Format)
- [Fallout Wiki: Critter FRM Nomenclature](https://falloutmods.fandom.com/wiki/Critter_FRM_nomenclature_(naming_system))
- [Fallout 2 Community Edition Source](https://github.com/alexbatalov/fallout2-ce)
- Legacy F2 Mapper Implementation (Dims Mapper)

---
*Last updated: 2025-01-28*
*This documentation reflects the implementation in Gecko Map Editor v2.0+*