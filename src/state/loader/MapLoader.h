#ifndef GECK_MAPPER_MAPLOADER_H
#define GECK_MAPPER_MAPLOADER_H

#include <filesystem>
#include <functional>
#include <thread>
#include <vector>
#include "Loader.h"

namespace geck {

class Map;
namespace resource {
class GameResources;
}

class MapLoader : public Loader {
public:
    MapLoader(std::shared_ptr<resource::GameResources> resources,
              const std::filesystem::path& mapPath,
              int elevation,
              bool forceFilesystem,
              std::function<void(std::unique_ptr<Map>)> onLoadCallback);
    ~MapLoader();

    void init() override;
    bool isDone() override;
    void onDone() override;

    bool hasError() const { return _hasError; }
    const std::string& errorMessage() const { return _errorMessage; }

private:
    bool ensureResourceListsReady();
    void load() override;
    void loadFromVFS();
    void loadFromFilesystem();
    void loadMapResources();

    std::atomic<bool> done = false;

    std::shared_ptr<resource::GameResources> _resources;
    std::filesystem::path _mapPath;
    std::unique_ptr<Map> _map;
    int _elevation;
    bool _forceFilesystem;
    std::function<void(std::unique_ptr<Map>)> _onLoadCallback;

    // Error handling
    std::string _errorMessage;
    bool _hasError = false;
};

} // namespace geck
#endif // GECK_MAPPER_MAPLOADER_H
