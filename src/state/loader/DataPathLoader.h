#pragma once

#include "Loader.h"
#include <filesystem>
#include <vector>
#include <functional>

namespace geck {

namespace resource {
class GameResources;
}

/**
 * @brief Loader for data paths (directories and DAT files)
 *
 * Shows progress while loading multiple data paths into GameResources.
 * Useful for initial application loading after configuration.
 */
class DataPathLoader : public Loader {
public:
    explicit DataPathLoader(std::shared_ptr<resource::GameResources> resources, const std::vector<std::filesystem::path>& dataPaths);
    ~DataPathLoader() override;

    void init() override;
    void load() override;
    bool isDone() override { return _done; }
    void onDone() override;
    bool hasError() const { return _hasError; }
    const std::string& errorMessage() const { return _errorMessage; }

private:
    std::shared_ptr<resource::GameResources> _resources;
    std::vector<std::filesystem::path> _dataPaths;
    std::atomic<bool> _done{ false };
    bool _hasError{ false };
    std::string _errorMessage;
    size_t _currentPathIndex{ 0 };
};

} // namespace geck
