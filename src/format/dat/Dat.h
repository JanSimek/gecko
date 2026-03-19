#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <filesystem>

namespace geck {

class DatEntry;

class Dat {
private:
    std::unordered_map<std::string, std::shared_ptr<DatEntry>> entries;

public:
    virtual ~Dat() = default;

    // NOTE: MSVC does not support std::filesystem::path as a key in std::unordered_map
    [[nodiscard]] const std::unordered_map<std::string, std::shared_ptr<DatEntry>>& getEntries() const;
    void addEntry(std::filesystem::path entryPath, std::shared_ptr<DatEntry> entry);
};

} // namespace geck
