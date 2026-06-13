# ProCommonFieldsWidget

A reusable Qt widget for editing PRO file common fields following DRY and KISS principles.

## Overview

The `ProCommonFieldsWidget` handles the 56-byte common header that appears in all PRO file types according to the Fallout 2 PRO file format specification:

- **ObjectType & ObjectID (PID)** - 4 bytes
- **TextID (Message ID)** - 4 bytes  
- **FrmType & FrmID (FID)** - 4 bytes
- **Light Radius** - 4 bytes
- **Light Intensity** - 4 bytes
- **Flags** - 4 bytes (+ extended flags)
- **Additional common item fields** (when applicable)

## Key Features

### DRY Implementation
- Consolidates common PRO editing functionality in one reusable widget
- Eliminates code duplication across different PRO type editors
- Provides standardized helper methods for widget creation

### KISS Design
- Simple, focused interface with logical field grouping
- Clear separation between basic, lighting, flags, and item-specific properties
- Straightforward load/save methods

### Based on Reference Implementation
- Field layout and validation based on F2 Mapper Dims analysis
- Material names and ranges match original tools
- Flag definitions follow Fallout 2 CE specifications

## Usage Example

```cpp
// Create the widget
auto commonFields = new ProCommonFieldsWidget(parent);

// Connect signals
connect(commonFields, &ProCommonFieldsWidget::fieldChanged, 
        this, &MyDialog::onFieldChanged);
connect(commonFields, &ProCommonFieldsWidget::fidSelectorRequested,
        this, &MyDialog::onFidSelectorRequested);
connect(commonFields, &ProCommonFieldsWidget::editMessageRequested,
        this, &MyDialog::onEditMessageRequested);

// Load from PRO file
commonFields->loadFromPro(proObject);

// Show/hide item-specific fields based on type
bool isItem = (proObject->type() == Pro::OBJECT_TYPE::ITEM);
commonFields->setItemFieldsVisible(isItem);

// Save changes back to PRO
commonFields->saveToPro(proObject);
```

## Widget Groups

### Basic Properties
- Object name/description (loaded from MSG files)
- PID (Object Type & ID)
- Message ID
- FID (Frame Type & ID) with selector button

### Lighting
- Light radius (0-8 hexes)
- Light intensity (0-65536)

### Object Flags
Core engine flags for rendering and behavior:
- Flat, No Block, Has Lighting
- Multi-hex, No Highlight  
- Transparency modes (Red, None, Wall, Glass, Steam, Energy)
- Light/Shoot Through

### Extended Properties
- Animation control (Primary/Secondary attack animations)
- Additional modding flags

### Item Properties (Items Only)
- Script ID
- Material type
- Container size, Weight, Base price
- Inventory FID
- Sound ID

## Integration with ProEditorDialog

The widget can be integrated into existing dialogs by:

1. Adding it to the dialog's layout
2. Connecting the signals for FID selection and message editing
3. Calling `loadFromPro()` when opening a PRO file
4. Calling `saveToPro()` before saving changes

## Validation

- All numeric fields have appropriate min/max ranges based on game limits
- Hex display for PID and FID values
- Tooltips provide context for each field
- Material dropdown uses proper names from reference implementation

## Constants

All game limits are defined as constants:
- `MAX_LIGHT_RADIUS = 8` (hexes)
- `MAX_LIGHT_INTENSITY = 65536` (0-100% intensity)
- `MAX_PID_VALUE = 0x5FFFFFF` (24-bit object ID)
- `MAX_MATERIAL_ID = 99`
- And many more following game specifications

This ensures consistency and prevents invalid values.