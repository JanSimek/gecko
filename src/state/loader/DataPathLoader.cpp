#include "DataPathLoader.h"
#include "../../util/ResourceManager.h"
#include <thread>
#include <spdlog/spdlog.h>

namespace geck {

DataPathLoader::DataPathLoader(const std::vector<std::filesystem::path>& dataPaths)
    : _dataPaths(dataPaths) {
}

DataPathLoader::~DataPathLoader() {
    if (_thread.joinable()) {
        _thread.join();
    }
}

void DataPathLoader::init() {
    _thread = std::thread{&DataPathLoader::load, this};
}

void DataPathLoader::load() {
    if (_dataPaths.empty()) {
        spdlog::warn("DataPathLoader: No data paths to load");
        _percentDone = 100;
        _done = true;
        return;
    }

    setStatus("Loading game data files");
    _percentDone = 0;
    _currentPathIndex = 0;

    auto& resourceManager = ResourceManager::getInstance();

    for (const auto& path : _dataPaths) {
        // Update progress
        setProgress("Loading: " + path.filename().string());
        
        try {
            spdlog::info("DataPathLoader: Loading data path: {}", path.string());
            resourceManager.addDataPath(path);
            
            // Update percentage
            _currentPathIndex++;
            _percentDone = static_cast<int>((_currentPathIndex * 100) / _dataPaths.size());
            
        } catch (const std::exception& e) {
            spdlog::error("DataPathLoader: Failed to load data path {}: {}", path.string(), e.what());
            _errorMessage = "Failed to load data path: " + path.string() + "\nError: " + e.what();
            _hasError = true;
            // Continue loading other paths
        }
    }

    if (_hasError) {
        setStatus("Loaded with errors");
    } else {
        setStatus("All data loaded successfully");
    }

    _percentDone = 100;
    _done = true;
}

void DataPathLoader::onDone() {
    if (_hasError) {
        spdlog::warn("DataPathLoader completed with errors: {}", _errorMessage);
    } else {
        spdlog::info("DataPathLoader completed successfully, loaded {} data paths", _dataPaths.size());
    }
}

} // namespace geck