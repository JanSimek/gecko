#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace geck {
namespace ui {
    namespace theme {

        /**
         * @brief Centralized color palette for consistent UI theming
         *
         * All colors used throughout the application should be defined here.
         * This enables future dark mode support and consistent visual design.
         */
        namespace colors {
            // Primary colors (selection, focus, accent)
            constexpr const char* PRIMARY = "#4A90E2";
            constexpr const char* PRIMARY_LIGHT = "#E6F2FF";
            constexpr const char* PRIMARY_DARK = "#0066CC";

            // Surface colors (backgrounds)
            constexpr const char* SURFACE = "#FFFFFF";
            constexpr const char* SURFACE_LIGHT = "#F9F9F9";
            constexpr const char* SURFACE_MEDIUM = "#F5F5F5";
            constexpr const char* SURFACE_DARK = "#F0F0F0";
            constexpr const char* SURFACE_HOVER = "#E0E0E0";

            // Border colors
            constexpr const char* BORDER = "#CCCCCC";
            constexpr const char* BORDER_LIGHT = "#E2E8F0";
            constexpr const char* BORDER_MEDIUM = "#D0D0D0";
            constexpr const char* BORDER_DARK = "gray";

            // Status colors
            constexpr const char* SUCCESS = "#4CAF50";
            constexpr const char* WARNING = "#F57C00";
            constexpr const char* STATUS_ERROR = "#D32F2F"; // Avoid Windows ERROR macro
            constexpr const char* INFO = "blue";

            // Text colors
            constexpr const char* TEXT_PRIMARY = "black";
            constexpr const char* TEXT_SECONDARY = "gray";
            constexpr const char* TEXT_MUTED = "#666666";

            // QColor constants for programmatic use (tree items, overlays, etc.)
            // Status colors (RGB for non-stylesheet contexts)
            inline QColor statusWarningRgb() { return QColor(245, 124, 0); }
            inline QColor statusSuccessRgb() { return QColor(56, 142, 60); }
            inline QColor statusInfoRgb() { return QColor(128, 128, 128); }

            // Selection highlight (semi-transparent green)
            inline QColor selectionHighlight() { return QColor(0, 255, 0, 100); }

            // Object category colors (for placeholder icons)
            inline QColor categoryItems() { return QColor(100, 150, 255); }
            inline QColor categoryScenery() { return QColor(100, 255, 100); }
            inline QColor categoryCritters() { return QColor(255, 150, 100); }
            inline QColor categoryWalls() { return QColor(150, 150, 150); }
            inline QColor categoryMisc() { return QColor(255, 100, 255); }

            // Quantity overlay text
            inline QColor quantityText() { return QColor(50, 255, 50); }

            // Invalid/error indicator
            inline QColor invalidPath() { return Qt::red; }

            // Basic text colors for painting
            inline QColor textDark() { return Qt::black; }
            inline QColor textLight() { return Qt::white; }

            // Primary color for selection/focus highlighting
            inline QColor primary() { return QColor(PRIMARY); }
        }

        /**
         * @brief Font constants for consistent typography
         */
        namespace fonts {
            // Small labels for overlays and compact displays
            // Note: Avoid 'small' - it's a Windows macro (rpcndr.h)
            inline QFont compactBold() { return QFont("Arial", 8, QFont::Bold); }
            inline QFont compact() { return QFont("Arial", 8); }
            inline QFont tiny() { return QFont("Arial", 6); }

            // Standard UI font (cross-platform friendly)
            inline QFont standard() { return QFont("Segoe UI", 9); }

            // Size-specific fonts for UI elements
            inline QFont small() { return QFont("Segoe UI", 10); }
            inline QFont title() { return QFont("Segoe UI", 14, QFont::Bold); }
            inline QFont largeTitle() { return QFont("Segoe UI", 16, QFont::DemiBold); }
            inline QFont statusText() { return QFont("Segoe UI", 11); }

            // Monospace for code/data display
            inline QFont monospace() { return QFont("Monaco, Consolas, 'Courier New', monospace", 10); }
            inline QFont monospaceBold() {
                QFont f("Monaco, Consolas, 'Courier New', monospace", 14);
                f.setBold(true);
                return f;
            }
        }

        /**
         * @brief Spacing constants for consistent layout
         */
        namespace spacing {
            constexpr int TIGHT = 4;    // Compact/nested layouts
            constexpr int NORMAL = 8;   // Standard widget spacing
            constexpr int LOOSE = 12;   // Dialog/group spacing
            constexpr int SECTION = 16; // Section separation

            // Margins
            constexpr int MARGIN_TIGHT = 4;
            constexpr int MARGIN_NORMAL = 8;
            constexpr int MARGIN_LOOSE = 12;
            constexpr int MARGIN_INDENT = 20;
        }

        /**
         * @brief Pre-built stylesheet strings for common UI patterns
         */
        namespace styles {
            // Selection states
            inline QString selectedWidget() {
                return QString("border: 2px solid %1; background-color: %2;")
                    .arg(colors::PRIMARY, colors::PRIMARY_LIGHT);
            }

            inline QString normalWidget() {
                return QString("border: 1px solid %1; background-color: %2;")
                    .arg(colors::BORDER_DARK, colors::SURFACE);
            }

            // Preview/display areas
            inline QString previewArea() {
                return QString("border: 1px solid %1; background-color: %2;")
                    .arg(colors::BORDER_DARK, colors::SURFACE_DARK);
            }

            // Input fields (read-only appearance)
            inline QString readOnlyInput() {
                return QString("QLineEdit { background-color: %1; }")
                    .arg(colors::SURFACE_LIGHT);
            }

            inline QString textAreaReadOnly() {
                return QString("QTextEdit { background-color: %1; border: 1px solid %2; margin: 0px; padding: 2px; }")
                    .arg(colors::SURFACE_LIGHT, colors::BORDER);
            }

            // Labels with borders (for FID displays, etc.)
            inline QString borderedLabel() {
                return QString("QLabel { border: 1px solid %1; padding: 2px 4px; background-color: %2; }")
                    .arg(colors::BORDER_LIGHT, colors::SURFACE);
            }

            // Status text styles
            inline QString statusNormal() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::TEXT_SECONDARY);
            }

            inline QString statusWarning() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::WARNING);
            }

            inline QString statusError() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::STATUS_ERROR);
            }

            inline QString statusSuccess() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::SUCCESS);
            }

            inline QString statusInfo() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::INFO);
            }

            // Help/small text
            inline QString helpText() {
                return QString("QLabel { color: %1; font-size: 11px; margin-bottom: 8px; }")
                    .arg(colors::TEXT_SECONDARY);
            }

            inline QString smallLabel() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::TEXT_SECONDARY);
            }

            // Priority/emphasis labels
            inline QString emphasisLabel() {
                return QString("font-weight: bold; color: %1;")
                    .arg(colors::PRIMARY_DARK);
            }

            // Bold label (for table/grid headers)
            inline QString boldLabel() {
                return QString("font-weight: bold;");
            }

            // Placeholder/disabled text
            inline QString placeholderText() {
                return QString("color: %1;")
                    .arg(colors::TEXT_SECONDARY);
            }

            // Error text with monospace
            inline QString errorMonospace() {
                return QString("color: %1; font-family: monospace;")
                    .arg(colors::STATUS_ERROR);
            }

            inline QString warningMonospace() {
                return QString("color: %1; font-family: monospace;")
                    .arg(colors::WARNING);
            }

            // Normal monospace text (for script/code display)
            inline QString monospaceText() {
                return QString("color: %1; font-family: monospace;")
                    .arg(colors::TEXT_PRIMARY);
            }

            // Progress bar (loading widget)
            inline QString progressBar() {
                return QString("background-color: %1;")
                    .arg(colors::SUCCESS);
            }

            // Slider styling
            inline QString compactSlider() {
                return QString(
                    "QSlider::groove:horizontal {"
                    "  border: 1px solid %1;"
                    "  height: 4px;"
                    "  background: %2;"
                    "}"
                    "QSlider::handle:horizontal {"
                    "  background: %3;"
                    "  width: 12px;"
                    "  margin: -4px 0;"
                    "}"
                    "QSlider::handle:horizontal:hover {"
                    "  background: %4;"
                    "}")
                    .arg(colors::BORDER_MEDIUM, colors::SURFACE_MEDIUM,
                        colors::SURFACE_MEDIUM, colors::SURFACE_HOVER);
            }

            // Overlay button (circular, semi-transparent for preview widgets)
            inline QString overlayButton() {
                return QString(
                    "QPushButton {"
                    "  background-color: rgba(255, 255, 255, 180);"
                    "  border: 1px solid rgba(0, 0, 0, 100);"
                    "  border-radius: 12px;"
                    "}"
                    "QPushButton:hover {"
                    "  background-color: rgba(255, 255, 255, 220);"
                    "  border-color: rgba(0, 0, 0, 150);"
                    "}"
                    "QPushButton:pressed {"
                    "  background-color: rgba(255, 255, 255, 255);"
                    "}");
            }

            // FID/file selector button (clickable label-like appearance)
            inline QString fidButton() {
                return QString(
                    "QPushButton {"
                    "  border: 1px solid %1;"
                    "  padding: 2px 4px;"
                    "  background-color: %2;"
                    "  text-align: left;"
                    "}"
                    "QPushButton:hover {"
                    "  background-color: %3;"
                    "  border-color: #999;"
                    "}"
                    "QPushButton:pressed {"
                    "  background-color: %4;"
                    "}")
                    .arg(colors::BORDER_MEDIUM, colors::SURFACE,
                        colors::SURFACE_MEDIUM, colors::SURFACE_HOVER);
            }

            // Bold group box header style
            inline QString boldGroupBox() {
                return QString("QGroupBox { font-weight: bold; }");
            }

            // Progress bar style (loading dialogs)
            inline QString progressBarStyle() {
                return QString(
                    "QProgressBar {"
                    "  border: 1px solid %1;"
                    "  border-radius: 3px;"
                    "  text-align: center;"
                    "  height: 20px;"
                    "}"
                    "QProgressBar::chunk {"
                    "  background-color: %2;"
                    "  border-radius: 2px;"
                    "}")
                    .arg(colors::BORDER, colors::SUCCESS);
            }

            // Muted/secondary text (build info, help text)
            inline QString mutedText() {
                return QString("QLabel { color: %1; font-size: 10pt; }")
                    .arg(colors::TEXT_MUTED);
            }

            // Italic secondary text
            inline QString italicSecondaryText() {
                return QString("QLabel { color: %1; font-style: italic; }")
                    .arg(colors::TEXT_SECONDARY);
            }

            // Compact label (small, no margin/padding for tight layouts)
            inline QString compactLabel() {
                return QString("QLabel { font-size: 10px; margin: 0px; padding: 0px; }");
            }

            // Title/header label (bold, larger)
            inline QString titleLabel() {
                return QString("QLabel { font-weight: bold; font-size: 14px; padding: 4px; }");
            }

            // Compact widget (no margin/padding/border)
            inline QString compactWidget() {
                return QString("QWidget { margin: 0px; padding: 0px; border: none; }");
            }

            // Transparent background widget
            inline QString transparentWidget() {
                return QString("QLabel { background-color: transparent; }");
            }

            // Bold label with bottom margin (dialog titles)
            inline QString boldLabelWithMargin() {
                return QString("QLabel { font-weight: bold; margin-bottom: %1px; }")
                    .arg(spacing::MARGIN_TIGHT);
            }
        }

    } // namespace theme
    /**
     * @brief UI styling constants and utilities
     *
     * Layout dimensions - widget sizes, dock and dialog geometry, column widths. Colours,
     * the spacing scale and prebuilt stylesheets live in the theme namespace above; the aliases
     * here exist so a layout can reach for a spacing value without qualifying it twice.
     */
    namespace constants {
        // Spacing aliases for clarity
        constexpr int SPACING_TIGHT = theme::spacing::TIGHT;
        constexpr int SPACING_NORMAL = theme::spacing::NORMAL;
        constexpr int SPACING_LOOSE = theme::spacing::LOOSE;
        constexpr int SPACING_SECTION = theme::spacing::SECTION;

        // Widget sizes
        constexpr int LIST_MAX_HEIGHT = 100;
        constexpr int LIST_MIN_HEIGHT = 160; // tall enough to show several data-path rows before scrolling
        constexpr int BUTTON_MIN_WIDTH = 80;

        /**
         * @brief Widget size constants for consistent UI elements
         */
        namespace sizes {
            // Button sizes
            constexpr int ICON_BUTTON = 24;
            constexpr int ICON_BUTTON_SMALL = 20;
            constexpr int ICON_BUTTON_HEIGHT = 22;   // Height for small icon buttons
            constexpr int NAV_BUTTON = 30;           // Navigation/pagination buttons
            constexpr int ACTION_BUTTON_HEIGHT = 30; // Min height for text+icon action buttons (no icon clipping)

            // Icon sizes
            constexpr int ICON_SIZE_SMALL = 18; // Compact button icons
            constexpr int ICON_SIZE_LARGE = 64; // Large icon (about dialog)

            // Field sizes
            constexpr int WIDTH_INPUT_SMALL = 40;  // Small spinbox width
            constexpr int WIDTH_INPUT_MEDIUM = 60; // Medium spinbox/input width

            // Label sizes
            constexpr int LABEL_MIN_WIDTH = 40;
            constexpr int LABEL_STANDARD = 80;
            constexpr int LABEL_FRAME = 30;       // Frame number labels
            constexpr int LABEL_FRAME_WIDE = 40;  // Wider frame labels
            constexpr int WIDTH_LABEL_SKILL = 90; // Skill name labels
            constexpr int WIDTH_TYPE_LABEL = 50;  // Damage type label width
            constexpr int HEIGHT_TYPE_LABEL = 19; // Damage type label height

            // Panel/widget sizes
            constexpr int WIDTH_INFO_PANEL = 288;   // Left info panel width
            constexpr int WIDTH_PLAY_BUTTON = 30;   // Animation play button
            constexpr int HEIGHT_DESCRIPTION = 80;  // Description text area
            constexpr int HEIGHT_PROGRESS_BAR = 16; // Progress bar height

            // Preview sizes
            constexpr int PREVIEW_SMALL = 80;
            constexpr int PREVIEW_TILE = 120;       // Tile/item previews
            constexpr int PREVIEW_TILE_HEIGHT = 96; // Tile preview height
            constexpr int PREVIEW_MEDIUM = 128;     // Object previews
            constexpr int PREVIEW_LARGE = 200;      // FRM selector preview

            // Panel sizes
            constexpr int PANEL_MIN_HEIGHT = 300; // Minimum panel height
            constexpr int WIDTH_PANEL_MIN = 300;  // Panel minimum width

            // Panel sizeHint dimensions
            constexpr int PANEL_PREFERRED_WIDTH = 360;  // Preferred panel width
            constexpr int PANEL_PREFERRED_HEIGHT = 250; // Preferred panel height
            constexpr int PANEL_MIN_SIZE_WIDTH = 200;   // Minimum sizeHint width
            constexpr int PANEL_MIN_SIZE_HEIGHT = 100;  // Minimum sizeHint height

            // Minimum widths for specific widgets
            constexpr int WIDTH_PID_FIELD_MIN = 120; // PID/filename field minimum
            constexpr int WIDTH_STATUS_LABEL = 200;  // Status label minimum
            constexpr int WIDTH_HEX_LABEL = 80;      // Hex index label
        }

        /**
         * @brief Dock widget size constants
         */
        namespace dock {
            constexpr int MIN_WIDTH = 100;        // Minimum dock width
            constexpr int MIN_HEIGHT_SMALL = 50;  // Small dock height (info panels)
            constexpr int MIN_HEIGHT_LARGE = 100; // Large dock height (palettes)
        }

        /**
         * @brief Font size constants
         */
        namespace fonts {
            constexpr int SIZE_TITLE = 14; // Title/version label font size
        }

        /**
         * @brief Column width constants for tables/tree views
         */
        namespace column_widths {
            constexpr int ICON = 80;         // Icon column
            constexpr int NAME_SHORT = 150;  // Short name column
            constexpr int NAME_MEDIUM = 180; // Medium name column
            constexpr int NAME_WIDE = 200;   // Wide name column
            constexpr int TYPE = 100;        // Type column
            constexpr int TYPE_WIDE = 120;   // Wide type column
            constexpr int AMOUNT = 60;       // Amount/quantity column (small)
            constexpr int AMOUNT_WIDE = 80;  // Amount column (wide)
            constexpr int PID = 80;          // PID column
        }

        /**
         * @brief Palette panel constants (tiles, objects)
         */
        namespace palette {
            constexpr int ITEMS_PER_PAGE = 200;
            constexpr int MAX_ITEMS_PER_ROW = 20;
            constexpr int DEFAULT_TILES_PER_ROW = 8;
            constexpr int DEFAULT_OBJECTS_PER_ROW = 6;
        }

        /**
         * @brief SFML-related constants
         */
        namespace sfml {
            constexpr uint8_t DRAG_PREVIEW_ALPHA = 180;
        }

        // Group box margins
        constexpr int GROUP_MARGIN = 8;           // Standard horizontal margin
        constexpr int GROUP_MARGIN_VERTICAL = 12; // Vertical margin with title padding
        constexpr int COMPACT_MARGIN = 4;         // Compact grid/palette layouts
        constexpr int DIALOG_PADDING = 20;        // Main dialog content padding
        constexpr int PANEL_CONTENT_MARGIN = 5;   // Panel content padding

        // Animation timing
        constexpr int ANIMATION_TIMER_INTERVAL = 200; // Animation frame interval (ms)

        // Layout spacing
        constexpr int SPACING_WIDE = 10;    // Wide spacing for major sections
        constexpr int SPACING_FORM = 6;     // Form field spacing
        constexpr int SPACING_GRID = 2;     // Compact grid spacing
        constexpr int SPACING_COLUMNS = 12; // Two-column layout spacing
        constexpr int SPACING_DIALOG = 15;  // Dialog layout spacing
        constexpr int PANEL_MARGIN = 8;     // Standard panel margin (uniform)

        /**
         * @brief Standard dialog size constants
         */
        namespace dialog_sizes {
            // Settings dialog
            constexpr int SETTINGS_MIN_WIDTH = 640; // Data Paths button row wraps, so no wide floor needed
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
        constexpr const char* READY_STATUS = "Ready";
    }

} // namespace ui
} // namespace geck
