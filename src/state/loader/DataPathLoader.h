#pragma once

#include "Loader.h"
#include <filesystem>
#include <vector>
#include <functional>

namespace geck {

/**
 * @brief Loader for data paths (directories and DAT files)
 * 
 * Shows progress while loading multiple data paths into the ResourceManager.
 * Useful for initial application loading after configuration.
 */
class DataPathLoader : public Loader {
public:
    explicit DataPathLoader(const std::vector<std::filesystem::path>& dataPaths);
    ~DataPathLoader() override;

    void init() override;
    void load() override;
    bool isDone() override { return _done; }
    void onDone() override;

private:
    std::vector<std::filesystem::path> _dataPaths;
    std::atomic<bool> _done{false};
    bool _hasError{false};
    std::string _errorMessage;
    size_t _currentPathIndex{0};
};

} // namespace geck