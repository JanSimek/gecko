#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "resource/GameResources.h"

#include "support/TempFile.h"

namespace geck::test {

/// A scratch data path: a unique TempDir the test fills with game files, plus a GameResources that
/// mounts it. Write the files before the first mount() — a mounted directory lists what existed
/// when it was mounted, so later writes need `files().refresh()`.
class DataTree {
public:
    explicit DataTree(const std::string& name)
        : _dir(name) { }

    DataTree(const DataTree&) = delete;
    DataTree& operator=(const DataTree&) = delete;

    const std::filesystem::path& root() const { return _dir.path(); }

    /// Writes `contents` at `relative` under the root; returns the absolute path.
    std::filesystem::path write(const std::filesystem::path& relative, std::string_view contents) const {
        const auto path = root() / relative;
        writeFile(path, contents);
        return path;
    }

    std::filesystem::path writeBytes(const std::filesystem::path& relative, const std::vector<uint8_t>& bytes) const {
        const auto path = root() / relative;
        test::writeBytes(path, bytes);
        return path;
    }

    /// The tree's GameResources with the root mounted as a data path (added on the first call only).
    resource::GameResources& mount() {
        if (!_mounted) {
            _resources.files().addDataPath(root());
            _mounted = true;
        }
        return _resources;
    }

private:
    TempDir _dir; // declared first: the mount is torn down before the tree is deleted
    resource::GameResources _resources;
    bool _mounted = false;
};

} // namespace geck::test
