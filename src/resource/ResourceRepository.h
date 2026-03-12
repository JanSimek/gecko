#pragma once

#include "DataFileSystem.h"

#include "../format/IFile.h"
#include "../reader/ReaderExceptions.h"
#include "../reader/ReaderFactory.h"

#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace geck::resource {

class ResourceRepository final {
public:
    explicit ResourceRepository(DataFileSystem& files);

    void clear();

    template <class T>
    [[nodiscard]] T* find(const std::filesystem::path& path) const {
        static_assert(std::is_base_of<IFile, T>::value, "Resource must derive from IFile");

        const std::string key = normalizeKey(path);
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

    template <class T>
    [[nodiscard]] T* load(const std::filesystem::path& path) {
        static_assert(std::is_base_of<IFile, T>::value, "Resource must derive from IFile");

        const std::string key = normalizeKey(path);
        if (auto* resource = find<T>(key)) {
            return resource;
        }

        auto rawData = _files.readRawBytes(path);
        if (!rawData) {
            throw IOException("Failed to open file from data paths", path);
        }

        auto resource = ReaderFactory::readFileFromMemory<T>(*rawData, key);
        T* resourcePtr = resource.get();
        _resources[key] = std::move(resource);
        return resourcePtr;
    }

private:
    static std::string normalizeKey(const std::filesystem::path& path);

    DataFileSystem& _files;
    std::unordered_map<std::string, std::unique_ptr<IFile>> _resources;
};

} // namespace geck::resource
