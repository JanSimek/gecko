#pragma once

namespace geck {
namespace ui {

/**
 * @brief UI styling constants and utilities
 * 
 * Centralizes common styling, margins, and UI configuration
 * to improve consistency across the application.
 */
namespace constants {
    // Margins and spacing
    constexpr int DEFAULT_MARGIN = 12;
    constexpr int DEFAULT_SPACING = 12;
    constexpr int INDENT_MARGIN = 20;
    constexpr int SMALL_SPACING = 8;
    
    // Widget sizes
    constexpr int LIST_MAX_HEIGHT = 100;
    constexpr int BUTTON_MIN_WIDTH = 80;
    constexpr int STEAM_APPID_WIDTH = 100;
    
    // Dialog sizes
    constexpr int SETTINGS_MIN_WIDTH = 780;
    constexpr int SETTINGS_MIN_HEIGHT = 620;
    constexpr int SETTINGS_DEFAULT_WIDTH = 900;
    constexpr int SETTINGS_DEFAULT_HEIGHT = 750;
}

namespace styles {
    // Common text styles
    constexpr const char* HELP_TEXT = "QLabel { color: gray; font-size: 11px; margin-bottom: 8px; }";
    constexpr const char* SMALL_LABEL = "QLabel { color: gray; font-size: 11px; }";
    constexpr const char* STATUS_NORMAL = "QLabel { color: gray; font-size: 11px; }";
    constexpr const char* STATUS_WARNING = "QLabel { color: orange; font-size: 11px; }";
    constexpr const char* STATUS_ERROR = "QLabel { color: red; font-size: 11px; }";
    constexpr const char* STATUS_SUCCESS = "QLabel { color: green; font-size: 11px; }";
    constexpr const char* STATUS_INFO = "QLabel { color: blue; font-size: 11px; }";
}

namespace defaults {
    // Default values
    constexpr const char* STEAM_APPID = "38410";
    constexpr const char* READY_STATUS = "Ready";
}

} // namespace ui
} // namespace geck
