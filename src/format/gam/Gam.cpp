#include "Gam.h"

#include <algorithm>
#include <stdexcept>

namespace geck {

Gam::Gam(const std::filesystem::path& path)
    : IFile(path) {
}

const std::string& Gam::gvarKey(size_t index) const {
    return _gvars.at(index).first;
}

int Gam::gvarValue(const std::string& key) {
    auto it = std::find_if(_gvars.begin(), _gvars.end(), [&key](std::pair<std::string, int> const& elem) {
        return elem.first == key;
    });
    if (it != _gvars.end()) {
        return it->second;
    }
    throw std::runtime_error{ "GVAR '" + key + "' not found" };
}

int Gam::gvarValue(size_t index) {
    if (index > _gvars.size()) {
        throw std::runtime_error{ "GVAR index " + std::to_string(index) + "' out of range" };
    }
    return _gvars.at(index).second;
}

const std::string& Gam::mvarKey(size_t index) const {
    return _mvars.at(index).first;
}

int Gam::mvarValue(const std::string& key) {
    auto it = std::find_if(_mvars.begin(), _mvars.end(), [&key](std::pair<std::string, int> const& elem) {
        return elem.first == key;
    });
    if (it != _mvars.end()) {
        return it->second;
    }
    throw std::runtime_error{ "MVAR '" + key + "' not found" };
}

int Gam::mvarValue(size_t index) {
    if (index > _mvars.size()) {
        throw std::runtime_error{ "MVAR index " + std::to_string(index) + "' out of range" };
    }
    return _mvars.at(index).second;
}

void Gam::addMvar(const std::string& key, int value) {
    _mvars.emplace_back(key, value);
}

void Gam::addGvar(const std::string& key, int value) {
    _gvars.emplace_back(key, value);
}

} // namespace geck
