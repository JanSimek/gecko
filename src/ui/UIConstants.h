#pragma once

#include "theme/ThemeManager.h"

namespace geck {
namespace ui {

    /**
     * @brief UI styling constants and utilities
     *
     * Centralizes common styling, margins, and UI configuration
     * to improve consistency across the application.
     *
     * NOTE: For colors and detailed styling, prefer using theme::colors
     * and theme::styles from ThemeManager.h. This file provides backward
     * compatibility and additional layout constants.
     */
    namespace constants {
        // Spacing aliases for clarity
        constexpr int SPACING_TIGHT = theme::spacing::TIGHT;
        constexpr int SPACING_NORMAL = theme::spacing::NORMAL;
        constexpr int SPACING_LOOSE = theme::spacing::LOOSE;
        constexpr int SPACING_SECTION = theme::spacing::SECTION;

        // Widget sizes
        constexpr int LIST_MAX_HEIGHT = 100;
        constexpr int BUTTON_MIN_WIDTH = 80;
        constexpr int STEAM_APPID_WIDTH = 100;

        /**
         * @brief Widget size constants for consistent UI elements
         */
        namespace sizes {
            // Button sizes
            constexpr int ICON_BUTTON = 24;
            constexpr int ICON_BUTTON_SMALL = 20;
            constexpr int ICON_BUTTON_HEIGHT = 22; // Height for small icon buttons
            constexpr int NAV_BUTTON = 30;  // Navigation/pagination buttons

            // Icon sizes
            constexpr int ICON_SIZE_SMALL = 18;  // Compact button icons
            constexpr int ICON_SIZE_LARGE = 64;  // Large icon (about dialog)

            // Field sizes
            constexpr int WIDTH_INPUT_SMALL = 40;   // Small spinbox width
            constexpr int WIDTH_INPUT_MEDIUM = 60;  // Medium spinbox/input width

            // Label sizes
            constexpr int LABEL_MIN_WIDTH = 40;
            constexpr int LABEL_STANDARD = 80;
            constexpr int LABEL_FRAME = 30;       // Frame number labels
            constexpr int LABEL_FRAME_WIDE = 40;  // Wider frame labels
            constexpr int WIDTH_LABEL_SKILL = 90; // Skill name labels
            constexpr int WIDTH_TYPE_LABEL = 50;  // Damage type label width
            constexpr int HEIGHT_TYPE_LABEL = 19; // Damage type label height

            // Panel/widget sizes
            constexpr int WIDTH_INFO_PANEL = 288;    // Left info panel width
            constexpr int WIDTH_PLAY_BUTTON = 30;    // Animation play button
            constexpr int HEIGHT_DESCRIPTION = 80;   // Description text area
            constexpr int HEIGHT_PROGRESS_BAR = 16;  // Progress bar height

            // Preview sizes
            constexpr int PREVIEW_SMALL = 80;
            constexpr int PREVIEW_TILE = 120;        // Tile/item previews
            constexpr int PREVIEW_TILE_HEIGHT = 96;  // Tile preview height
            constexpr int PREVIEW_MEDIUM = 128;      // Object previews
            constexpr int PREVIEW_LARGE = 200;       // FRM selector preview

            // Panel sizes
            constexpr int PANEL_MIN_HEIGHT = 300;  // Minimum panel height
            constexpr int WIDTH_PANEL_MIN = 300;   // Panel minimum width

            // Panel sizeHint dimensions
            constexpr int PANEL_PREFERRED_WIDTH = 360;   // Preferred panel width
            constexpr int PANEL_PREFERRED_HEIGHT = 250;  // Preferred panel height
            constexpr int PANEL_MIN_SIZE_WIDTH = 200;    // Minimum sizeHint width
            constexpr int PANEL_MIN_SIZE_HEIGHT = 100;   // Minimum sizeHint height

            // Minimum widths for specific widgets
            constexpr int WIDTH_PID_FIELD_MIN = 120;  // PID/filename field minimum
            constexpr int WIDTH_STATUS_LABEL = 200;   // Status label minimum
            constexpr int WIDTH_HEX_LABEL = 80;       // Hex index label
        }

        /**
         * @brief Dock widget size constants
         */
        namespace dock {
            constexpr int MIN_WIDTH = 100;          // Minimum dock width
            constexpr int MIN_HEIGHT_SMALL = 50;    // Small dock height (info panels)
            constexpr int MIN_HEIGHT_LARGE = 100;   // Large dock height (palettes)
        }

        /**
         * @brief Font size constants
         */
        namespace fonts {
            constexpr int SIZE_TITLE = 14;  // Title/version label font size
        }

        /**
         * @brief Column width constants for tables/tree views
         */
        namespace column_widths {
            constexpr int ICON = 80;           // Icon column
            constexpr int NAME_SHORT = 150;    // Short name column
            constexpr int NAME_MEDIUM = 180;   // Medium name column
            constexpr int NAME_WIDE = 200;     // Wide name column
            constexpr int TYPE = 100;          // Type column
            constexpr int TYPE_WIDE = 120;     // Wide type column
            constexpr int AMOUNT = 60;         // Amount/quantity column (small)
            constexpr int AMOUNT_WIDE = 80;    // Amount column (wide)
            constexpr int PID = 80;            // PID column
        }

        // Group box margins
        constexpr int GROUP_MARGIN = 8;              // Standard horizontal margin
        constexpr int GROUP_MARGIN_VERTICAL = 12;    // Vertical margin with title padding
        constexpr int COMPACT_MARGIN = 4;            // Compact grid/palette layouts
        constexpr int DIALOG_PADDING = 20;           // Main dialog content padding
        constexpr int PANEL_CONTENT_MARGIN = 5;      // Panel content padding

        // Animation timing
        constexpr int ANIMATION_TIMER_INTERVAL = 200; // Animation frame interval (ms)

        // Layout spacing
        constexpr int SPACING_WIDE = 10;     // Wide spacing for major sections
        constexpr int SPACING_FORM = 6;      // Form field spacing
        constexpr int SPACING_GRID = 2;      // Compact grid spacing
        constexpr int SPACING_COLUMNS = 12;  // Two-column layout spacing
        constexpr int SPACING_DIALOG = 15;   // Dialog layout spacing
        constexpr int PANEL_MARGIN = 8;      // Standard panel margin (uniform)

        /**
         * @brief Standard dialog size constants
         */
        namespace dialog_sizes {
            // Settings dialog
            constexpr int SETTINGS_MIN_WIDTH = 780;
            constexpr int SETTINGS_MIN_HEIGHT = 620;
            constexpr int SETTINGS_DEFAULT_WIDTH = 900;
            constexpr int SETTINGS_DEFAULT_HEIGHT = 750;
            // Large dialogs (FRM selector, inventory viewer)
            constexpr int LARGE_WIDTH = 800;
            constexpr int LARGE_HEIGHT = 600;
            // Medium dialogs (message selector)
            constexpr int MEDIUM_WIDTH = 500;
            constexpr int MEDIUM_HEIGHT = 400;
            // Small dialogs (exit grid properties)
            constexpr int SMALL_WIDTH = 400;
            constexpr int SMALL_HEIGHT = 300;
            // About dialog
            constexpr int ABOUT_WIDTH = 400;
            constexpr int ABOUT_HEIGHT = 200;
            // Loading widget
            constexpr int LOADING_WIDTH = 400;
            constexpr int LOADING_HEIGHT = 150;
            // PRO editor (unique size)
            constexpr int PRO_EDITOR_WIDTH = 950;
            constexpr int PRO_EDITOR_HEIGHT = 650;
            // Main window
            constexpr int MAIN_WINDOW_MIN_WIDTH = 1024;
            constexpr int MAIN_WINDOW_MIN_HEIGHT = 768;
        }
    }

    namespace defaults {
        // Default values
        constexpr const char* STEAM_APPID = "38410";
        constexpr const char* READY_STATUS = "Ready";
    }

} // namespace ui
} // namespace geck
