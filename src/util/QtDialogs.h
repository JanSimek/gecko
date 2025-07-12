#pragma once

#include <string>
#include <vector>
#include <filesystem>

class QString;

namespace geck {

/**
 * Qt-based file dialogs to replace portable-file-dialogs
 * Provides the same interface for gradual migration
 */
class QtDialogs {
public:
    /**
     * Open file dialog
     * @param title Dialog title
     * @param default_path Default directory path
     * @param filters File type filters (e.g., {"Fallout Maps", "*.map"})
     * @return Selected file path or empty string if cancelled
     */
    static std::string openFile(const std::string& title, 
                               const std::string& default_path = "",
                               const std::vector<std::pair<std::string, std::string>>& filters = {});

    /**
     * Save file dialog
     * @param title Dialog title
     * @param default_path Default directory path
     * @param filters File type filters
     * @param force_overwrite Whether to allow overwriting existing files
     * @return Selected file path or empty string if cancelled
     */
    static std::string saveFile(const std::string& title,
                               const std::string& default_path = "",
                               const std::vector<std::pair<std::string, std::string>>& filters = {},
                               bool force_overwrite = false);

    /**
     * Select folder dialog
     * @param title Dialog title
     * @param default_path Default directory path
     * @return Selected folder path or empty string if cancelled
     */
    static std::string selectFolder(const std::string& title,
                                   const std::string& default_path = "");

private:
    // Initialize Qt application if needed
    static void ensureQtApplication();
    
    // Convert filters to Qt format
    static QString formatFilters(const std::vector<std::pair<std::string, std::string>>& filters);
};

} // namespace geck