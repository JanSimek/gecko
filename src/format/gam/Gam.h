#pragma once

#include <string>
#include <vector>

#include "format/IFile.h"

namespace geck {

class Gam : public IFile {
public:
    Gam(const std::filesystem::path& path);

    const std::string& gvarKey(size_t index) const;
    int gvarValue(const std::string& key);
    int gvarValue(size_t index);
    size_t gvarCount() const { return _gvars.size(); }

    const std::string& mvarKey(size_t index) const;
    int mvarValue(const std::string& key);
    int mvarValue(size_t index);
    size_t mvarCount() const { return _mvars.size(); }

    void addMvar(const std::string& key, int value);
    void addGvar(const std::string& key, int value);

private:
    std::vector<std::pair<std::string, int>> _gvars;
    std::vector<std::pair<std::string, int>> _mvars;
};

} // namespace geck
