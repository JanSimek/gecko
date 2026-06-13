# Icon Resources

This directory contains icon resources for the Gecko Map Editor UI.

## Tabler Icons

The application uses Tabler Icons (https://tabler-icons.io/) for a consistent and modern UI.

### Required Icons

To download the required Tabler icons, visit https://tabler-icons.io/ and download the following SVG icons:

#### Actions (`icons/tabler/actions/`)
- `file-plus.svg` - New file
- `folder-open.svg` - Open file/folder  
- `device-floppy.svg` - Save
- `rotate-clockwise.svg` - Rotate
- `pointer.svg` - Selection tool
- `settings.svg` - Preferences
- `power.svg` - Quit
- `select-all.svg` - Select all
- `square-off.svg` - Deselect
- `border-all.svg` - Scroll blocker

#### View Actions (`icons/tabler/actions/`)
- `box.svg` - View objects
- `user.svg` - View critters
- `wall.svg` - View walls (custom icon may be needed)
- `home-2.svg` - View roofs
- `grid-dots.svg` - View grid
- `brightness.svg` - View lighting

#### File Types (`icons/tabler/filetypes/`)
- `map-2.svg` - Map files
- `photo.svg` - Image files (FRM)
- `file-settings.svg` - Properties files (PRO)
- `message-2.svg` - Message files (MSG)
- `database.svg` - Data files (DAT)
- `list.svg` - List files (LST)
- `file-code.svg` - Script files (INT/SSL)
- `palette.svg` - Palette files (PAL)
- `file.svg` - Default file
- `folder.svg` - Folder closed
- `folder-open.svg` - Folder open

#### UI Elements (`icons/tabler/ui/`)
- `refresh.svg` - Refresh
- `grid-dots.svg` - Grid
- `plus.svg` - Add
- `minus.svg` - Remove
- `search.svg` - Search
- `cpu.svg` - Auto-detect
- `alert-triangle.svg` - Warning
- `alert-circle.svg` - Error
- `info-circle.svg` - Info

#### Panel Icons (`icons/tabler/panels/`)
- `info-circle.svg` - Info panel
- `select.svg` - Selection panel
- `layout-grid.svg` - Tile palette
- `box-multiple.svg` - Object palette
- `files.svg` - File browser

### Icon Format

All Tabler icons should be:
- SVG format
- 24x24 viewBox
- Stroke width: 2
- Stroke color: currentColor (for theme support)

### Alternative: Batch Download

You can also clone the Tabler Icons repository and copy the required icons:

```bash
git clone https://github.com/tabler/tabler-icons.git
cd tabler-icons/icons
# Copy required icons to the appropriate directories
```

### Custom Icons

For any icons not available in Tabler (like specific Fallout-related icons), create custom SVGs following the same style:
- 24x24 viewBox
- 2px stroke width
- Outline style to match Tabler

## License

Tabler Icons are MIT licensed. See https://github.com/tabler/tabler-icons/blob/master/LICENSE for details.