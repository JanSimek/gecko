#include <filesystem>

#include "Dat.h"
#include "DatEntry.h"

namespace geck {

std::unordered_map<std::string, std::shared_ptr<DatEntry>> const& Dat::getEntries() const {
    return this->entries;
}

void Dat::addEntry(const std::filesystem::path entryPath, std::shared_ptr<DatEntry> entry) {
    this->entries.emplace(entryPath.string(), entry);
}

} // namespace geck
