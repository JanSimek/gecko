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
        // Spacing - use theme::spacing for new code
        // These are kept for backward compatibility
        constexpr int DEFAULT_MARGIN = theme::spacing::LOOSE;
        constexpr int DEFAULT_SPACING = theme::spacing::LOOSE;
        constexpr int INDENT_MARGIN = theme::spacing::MARGIN_INDENT;
        constexpr int SMALL_SPACING = theme::spacing::NORMAL;

        // Spacing aliases for clarity
        constexpr int SPACING_TIGHT = theme::spacing::TIGHT;
        constexpr int SPACING_NORMAL = theme::spacing::NORMAL;
        constexpr int SPACING_LOOSE = theme::spacing::LOOSE;
        constexpr int SPACING_SECTION = theme::spacing::SECTION;

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

    namespace defaults {
        // Default values
        constexpr const char* STEAM_APPID = "38410";
        constexpr const char* READY_STATUS = "Ready";
    }

} // namespace ui
} // namespace geck
