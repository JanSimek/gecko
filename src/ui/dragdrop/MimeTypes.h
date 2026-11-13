#pragma once

namespace geck {
namespace ui {
    namespace mime {

        /**
         * @brief MIME type constants for drag and drop operations
         *
         * Centralizes all MIME type strings to avoid magic strings throughout the codebase.
         * Use these constants when setting or checking MIME data in drag/drop operations.
         */

        /// MIME type for object data (from palette or object list)
        constexpr const char* GECK_OBJECT = "application/x-geck-object";

    } // namespace mime
} // namespace ui
} // namespace geck
