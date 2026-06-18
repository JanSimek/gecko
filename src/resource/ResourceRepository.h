#pragma once

#include "DataFileSystem.h"

#include "format/IFile.h"
#include "reader/ReaderExceptions.h"
#include "reader/ReaderFactory.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <stdexcept>

namespace geck::resource {

/**
 * @brief Thread-safe cache of parsed game resources (FRM, PRO, LST, PAL, ...).
 *
 * Parsing is pure CPU/IO work, so the repository may be used concurrently from
 * the main thread and from background loaders (MapLoader, DataPathLoader). The
 * internal cache is guarded by a mutex; reading and parsing the underlying
 * bytes happens outside the lock so concurrent loads of different resources are
 * not serialized behind one another. Returned pointers remain valid until
 * clear(), which must only be called once no other thread is using the cache.
 */
class ResourceRepository final {
public:
    explicit ResourceRepository(DataFileSystem& files);

    void clear();

    template <class T>
    [[nodiscard]] T* find(const std::filesystem::path& path) const {
        static_assert(std::is_base_of<IFile, T>::value, "Resource must derive from IFile");

        const std::string key = normalizeKey(path);
        const std::lock_guard<std::mutex> lock(_mutex);
        return findLocked<T>(key);
    }

    template <class T>
    [[nodiscard]] T* load(const std::filesystem::path& path) {
        static_assert(std::is_base_of<IFile, T>::value, "Resource must derive from IFile");

        const std::string key = normalizeKey(path);

        {
            const std::lock_guard<std::mutex> lock(_mutex);
            if (auto* resource = findLocked<T>(key)) {
                return resource;
            }
        }

        // Read and parse outside the lock: this is the expensive part and must
        // not serialize concurrent loads of unrelated resources.
        auto rawData = _files.readRawBytes(path);
        if (!rawData) {
            throw IOException("Failed to open file from data paths", path);
        }
        auto resource = ReaderFactory::readFileFromMemory<T>(*rawData, key);

        const std::lock_guard<std::mutex> lock(_mutex);
        // Another thread may have cached the same key while we parsed; keep the
        // existing entry and drop our duplicate to preserve pointer stability.
        if (auto* existing = findLocked<T>(key)) {
            return existing;
        }
        T* resourcePtr = resource.get();
        _resources[key] = std::move(resource);
        return resourcePtr;
    }

private:
    // Caller must hold _mutex.
    template <class T>
    [[nodiscard]] T* findLocked(const std::string& key) const {
        auto iter = _resources.find(key);
        if (iter == _resources.end()) {
            return nullptr;
        }

        auto* resource = dynamic_cast<T*>(iter->second.get());
        if (!resource) {
            throw std::runtime_error("Requested file type does not match cached resource: " + key);
        }

        return resource;
    }

    static std::string normalizeKey(const std::filesystem::path& path);

    DataFileSystem& _files;
    std::unordered_map<std::string, std::unique_ptr<IFile>> _resources;
    mutable std::mutex _mutex;
};

} // namespace geck::resource
