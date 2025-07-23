# Resource Management System

## Overview

The new resource management system provides a modern, type-safe, and testable architecture for loading and caching game resources. It replaces the singleton-based `ResourceManager` with a modular system based on dependency injection.

## Architecture

### Core Components

1. **IResourceProvider**: Interface for loading raw data from various sources
   - `VirtualFileManager`: Implementation using vfspp for files and DAT archives

2. **IResourceLoader<T>**: Type-safe interface for parsing specific resource formats
   - `FrmLoader`: Loads FRM (graphics) files
   - `PalLoader`: Loads PAL (palette) files
   - `LstLoader`: Loads LST (list) files
   - `ProLoader`: Loads PRO (prototype) files
   - `MsgLoader`: Loads MSG (message) files

3. **ResourceCache<T>**: Generic caching with configurable eviction policies
   - LRU (Least Recently Used)
   - LFU (Least Frequently Used)
   - FIFO (First In First Out)
   - TTL (Time To Live)

4. **TextureManager**: Specialized manager for SFML textures
   - Automatic FRM to texture conversion
   - Palette support
   - Memory-aware caching

5. **FrmToTextureConverter**: Converts Fallout graphics to SFML textures
   - Configurable transparency and brightness
   - Frame stitching support

6. **ResourceSystem**: Main facade that ties everything together

## Usage

### Basic Usage

```cpp
#include "resource/ResourceSystem.h"

// Create and initialize resource system
auto resourceSystem = geck::resource::createFallout2ResourceSystem("path/to/fallout2");

// Load resources
auto frm = resourceSystem->getResource<Frm>("art/items/gun.frm");
auto lst = resourceSystem->getResource<Lst>("art/tiles/tiles.lst");

// Get textures
auto& textureMgr = resourceSystem->getTextureManager();
auto texture = textureMgr.getTexture("art/items/gun.frm");
```

### Dependency Injection

```cpp
class EditorWidget {
    std::shared_ptr<ResourceSystem> _resources;
    
public:
    EditorWidget(std::shared_ptr<ResourceSystem> resources)
        : _resources(std::move(resources)) {}
    
    void loadSprite(const std::string& path) {
        auto texture = _resources->getTextureManager().getTexture(path);
        // Use texture...
    }
};
```

### Custom Configuration

```cpp
ResourceSystem::Config config;
config.defaultCacheSize = 500;
config.defaultCacheMemory = 256 * 1024 * 1024; // 256MB

config.textureConfig.cacheConfig.policy = EvictionPolicy::LRU;
config.textureConfig.cacheConfig.maxSizeBytes = 1024 * 1024 * 1024; // 1GB
config.textureConfig.frmConfig.brightnessMultiplier = 4;

auto system = std::make_unique<ResourceSystem>(config);
```

## Migration Guide

### From Singleton ResourceManager

The old code:
```cpp
auto& rm = ResourceManager::getInstance();
rm.addDataPath("data");
auto texture = rm.texture("art/items/gun.frm");
```

Becomes:
```cpp
// In initialization
_resourceSystem = createFallout2ResourceSystem("data");

// In usage
auto texture = _resourceSystem->getTextureManager().getTexture("art/items/gun.frm");
```

### Using Compatibility Layer

For gradual migration, use `ResourceManagerCompat`:

```cpp
// Initialize once at startup
auto system = createFallout2ResourceSystem("data");
ResourceManagerCompat::initialize(system);

// Old code continues to work
auto& rm = ResourceManager::getInstance();
auto texture = rm.texture("art/items/gun.frm");
```

### Step-by-Step Migration

1. **Phase 1**: Add ResourceSystem to Application class
   ```cpp
   class Application {
       std::shared_ptr<ResourceSystem> _resourceSystem;
       // Keep old ResourceManager for now
   };
   ```

2. **Phase 2**: Update UI components to accept ResourceSystem
   ```cpp
   MainWindow(std::shared_ptr<ResourceSystem> resources);
   EditorWidget(std::shared_ptr<ResourceSystem> resources);
   ```

3. **Phase 3**: Replace ResourceManager calls
   - `ResourceManager::getInstance().texture()` → `_resources->getTextureManager().getTexture()`
   - `ResourceManager::getInstance().getResource<T>()` → `_resources->getResource<T>()`

4. **Phase 4**: Remove singleton usage completely

## Benefits

1. **Testability**: Easy to mock and test with dependency injection
2. **Type Safety**: Compile-time type checking, no dynamic_cast
3. **Performance**: Configurable caching with memory limits
4. **Flexibility**: Pluggable loaders and providers
5. **Maintainability**: Clear separation of concerns
6. **Thread Safety**: Built-in thread-safe caching

## Memory Management

The system provides fine-grained control over memory usage:

```cpp
// Monitor cache usage
auto stats = resourceSystem->getCacheStats<Frm>();
spdlog::info("FRM cache hit rate: {:.2f}%", stats.hitRate() * 100);

// Clear specific cache
resourceSystem->getTextureManager().clearCache();

// Clear all caches
resourceSystem->clearAllCaches();
```

## Extending the System

### Adding New Resource Types

1. Create the format class:
```cpp
class Map : public IFile {
    // Map data...
};
```

2. Create a loader:
```cpp
class MapLoader : public IResourceLoader<Map> {
    std::expected<std::unique_ptr<Map>, ResourceError> parse(
        const std::vector<uint8_t>& data,
        const std::filesystem::path& path) override {
        // Parse map data...
    }
};
```

3. Register with ResourceSystem:
```cpp
system->registerLoader<Map>(std::make_shared<MapLoader>());
```

4. Use it:
```cpp
auto map = system->getResource<Map>("maps/arroyo.map");
```

## Performance Considerations

- Cache sizes should be tuned based on available memory
- Use TTL eviction for resources that change frequently
- LRU eviction is best for general-purpose caching
- Monitor cache hit rates to optimize configuration
- Consider preloading critical resources

## Future Improvements

- Async resource loading
- Resource dependencies and reference counting
- Compression support
- Network resource providers
- Resource hot-reloading for development