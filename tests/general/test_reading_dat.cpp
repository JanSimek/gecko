#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <filesystem>

#include "format/dat/Dat.h"
#include "format/dat/DatEntry.h"
#include "reader/dat/DatReader.h"
#include "support/Fixtures.h"

using namespace geck;

namespace {

// f2_res.dat (the Fallout2-CE hi-res resource archive) is a valid DAT2 with 178
// zlib-compressed entries (also pinned by the vfspp-based qt test).
constexpr size_t F2_RES_ENTRY_COUNT = 178;

const DatEntry* findBySuffix(const Dat& dat, const std::string& suffix) {
    for (const auto& [name, entry] : dat.getEntries()) {
        if (name.size() >= suffix.size()
            && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return entry.get();
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("DatReader parses the f2_res.dat entry table", "[dat]") {
    DatReader reader;
    const auto dat = reader.openFile(geck::test::dataPath("f2_res.dat"));

    REQUIRE(dat != nullptr);
    CHECK(dat->getEntries().size() == F2_RES_ENTRY_COUNT);
}

TEST_CASE("DatReader normalizes names and reads entry metadata", "[dat]") {
    const size_t fileSize = std::filesystem::file_size(geck::test::dataPath("f2_res.dat"));

    DatReader reader;
    const auto dat = reader.openFile(geck::test::dataPath("f2_res.dat"));
    REQUIRE(dat != nullptr);

    // Names are normalized to lowercase, forward-slash, and key the entry map.
    const DatEntry* entry = findBySuffix(*dat, "hr_alltlk.frm");
    REQUIRE(entry != nullptr);
    CHECK(entry->getFilename() == "art/intrface/hr_alltlk.frm");
    CHECK(entry->getCompressed());
    CHECK(entry->getDecompressedSize() > 0);
    CHECK(entry->getPackedSize() > 0);
    CHECK(entry->getOffset() < fileSize);

    // Every entry has a normalized name keying itself and an in-bounds offset.
    for (const auto& [name, e] : dat->getEntries()) {
        CHECK_FALSE(name.empty());
        CHECK(name == e->getFilename());
        CHECK(name.find('\\') == std::string::npos);
        CHECK(e->getOffset() < fileSize);
    }
}

TEST_CASE("DatReader rejects a file too small to be a DAT archive", "[dat]") {
    DatReader reader;
    REQUIRE_THROWS(reader.openFile("tiny.dat", std::vector<uint8_t>{ 1, 2, 3, 4 }));
}
