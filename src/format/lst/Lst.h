#pragma once

#include <fstream>
#include <vector>

#include "../IFile.h"

namespace geck {

class Lst : public IFile {
private:
    std::vector<std::string> _list;

public:
    Lst(std::filesystem::path path)
        : IFile(path) { }

    const std::vector<std::string>& list() const;
    void setList(const std::vector<std::string>& list);

    const std::string& at(int line) const;
};

} // namespace geck
