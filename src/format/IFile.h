#pragma once

#include <filesystem>
#include <string>

namespace geck {

class IFile {
public:
    IFile(const std::filesystem::path& path)
        : _filename(path.filename().string())
        , _path(path) { }
    virtual ~IFile() = default;

    const std::string& filename() const { return _filename; }
    const std::filesystem::path& path() const { return _path; }

protected:
    std::string _filename;
    std::filesystem::path _path;
};

} // geck
