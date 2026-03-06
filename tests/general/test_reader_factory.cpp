#include <catch2/catch_test_macros.hpp>

#include "reader/ReaderFactory.h"
#include "format/pro/Pro.h"
#include "format/frm/Frm.h"
#include "format/gam/Gam.h"
#include "format/msg/Msg.h"
#include "format/lst/Lst.h"
#include "format/pal/Pal.h"

TEST_CASE("ReaderFactory Format Detection", "[reader_factory]") {
    using namespace geck;

    SECTION("Extension-based detection") {
        REQUIRE(ReaderFactory::detectFormat("test.pro") == ReaderFactory::Format::PRO);
        REQUIRE(ReaderFactory::detectFormat("test.frm") == ReaderFactory::Format::FRM);
        REQUIRE(ReaderFactory::detectFormat("test.dat") == ReaderFactory::Format::DAT);
        REQUIRE(ReaderFactory::detectFormat("test.pal") == ReaderFactory::Format::PAL);
        REQUIRE(ReaderFactory::detectFormat("test.gam") == ReaderFactory::Format::GAM);
        REQUIRE(ReaderFactory::detectFormat("test.msg") == ReaderFactory::Format::MSG);
        REQUIRE(ReaderFactory::detectFormat("test.lst") == ReaderFactory::Format::LST);
        REQUIRE(ReaderFactory::detectFormat("test.map") == ReaderFactory::Format::MAP);

        // Case insensitive
        REQUIRE(ReaderFactory::detectFormat("TEST.PRO") == ReaderFactory::Format::PRO);

        // Unknown extension
        REQUIRE(ReaderFactory::detectFormat("test.unknown") == ReaderFactory::Format::UNKNOWN);
    }

    SECTION("Magic number detection") {
        // FRM format - version 4 at start
        std::vector<uint8_t> frm_data = { 0x00, 0x00, 0x00, 0x04, 0x00, 0x10 }; // Version 4, FPS 16
        REQUIRE(ReaderFactory::detectFormat(frm_data, "test") == ReaderFactory::Format::FRM);
    }

    SECTION("Content-based detection") {
        // GAM format detection
        std::vector<uint8_t> gam_data;
        std::string gam_content = "GAME_GLOBAL_VARS:\ntest_var:=42;\n";
        gam_data.assign(gam_content.begin(), gam_content.end());
        REQUIRE(ReaderFactory::detectFormat(gam_data, "test") == ReaderFactory::Format::GAM);

        // MSG format detection
        std::vector<uint8_t> msg_data;
        std::string msg_content = "{100}{}{Hello World}";
        msg_data.assign(msg_content.begin(), msg_content.end());
        REQUIRE(ReaderFactory::detectFormat(msg_data, "test") == ReaderFactory::Format::MSG);

        // LST format detection (plain text)
        std::vector<uint8_t> lst_data;
        std::string lst_content = "file1.frm\nfile2.frm\nfile3.frm\n";
        lst_data.assign(lst_content.begin(), lst_content.end());
        REQUIRE(ReaderFactory::detectFormat(lst_data, "test") == ReaderFactory::Format::LST);
    }
}

TEST_CASE("ReaderFactory Reader Creation", "[reader_factory]") {
    using namespace geck;

    SECTION("Template-based reader creation") {
        auto pro_reader = ReaderFactory::createReader<Pro>(ReaderFactory::Format::PRO);
        REQUIRE(pro_reader != nullptr);

        auto frm_reader = ReaderFactory::createReader<Frm>(ReaderFactory::Format::FRM);
        REQUIRE(frm_reader != nullptr);

        auto gam_reader = ReaderFactory::createReader<Gam>(ReaderFactory::Format::GAM);
        REQUIRE(gam_reader != nullptr);

        auto msg_reader = ReaderFactory::createReader<Msg>(ReaderFactory::Format::MSG);
        REQUIRE(msg_reader != nullptr);
    }

    SECTION("Format mismatch throws exception") {
        REQUIRE_THROWS_AS(
            ReaderFactory::createReader<Pro>(ReaderFactory::Format::FRM),
            UnsupportedFormatException);
    }
}

TEST_CASE("ReaderFactory Utility Methods", "[reader_factory]") {
    using namespace geck;

    SECTION("Format support checking") {
        REQUIRE(ReaderFactory::isFormatSupported(ReaderFactory::Format::PRO));
        REQUIRE(ReaderFactory::isFormatSupported(ReaderFactory::Format::FRM));
        REQUIRE_FALSE(ReaderFactory::isFormatSupported(ReaderFactory::Format::UNKNOWN));

        REQUIRE(ReaderFactory::isFormatSupported(".pro"));
        REQUIRE(ReaderFactory::isFormatSupported(".PRO")); // Case insensitive
        REQUIRE_FALSE(ReaderFactory::isFormatSupported(".unknown"));
    }

    SECTION("Supported extensions list") {
        auto extensions = ReaderFactory::getSupportedExtensions();
        REQUIRE_FALSE(extensions.empty());
        REQUIRE(std::find(extensions.begin(), extensions.end(), ".pro") != extensions.end());
        REQUIRE(std::find(extensions.begin(), extensions.end(), ".frm") != extensions.end());
    }

    SECTION("Format info retrieval") {
        auto pro_info = ReaderFactory::getFormatInfo(ReaderFactory::Format::PRO);
        REQUIRE(pro_info.format == ReaderFactory::Format::PRO);
        REQUIRE(pro_info.name == "Fallout PRO Object");
        REQUIRE_FALSE(pro_info.extensions.empty());
        REQUIRE(pro_info.min_file_size == 24);

        auto unknown_info = ReaderFactory::getFormatInfo(ReaderFactory::Format::UNKNOWN);
        REQUIRE(unknown_info.format == ReaderFactory::Format::UNKNOWN);
    }

    SECTION("All supported formats") {
        auto formats = ReaderFactory::getAllSupportedFormats();
        REQUIRE(formats.size() == 8); // DAT, PRO, FRM, PAL, GAM, MSG, LST, MAP

        // Check that all expected formats are present
        bool found_pro = false, found_frm = false;
        for (const auto& info : formats) {
            if (info.format == ReaderFactory::Format::PRO)
                found_pro = true;
            if (info.format == ReaderFactory::Format::FRM)
                found_frm = true;
        }
        REQUIRE(found_pro);
        REQUIRE(found_frm);
    }
}

TEST_CASE("ReaderFactory File Grouping", "[reader_factory]") {
    using namespace geck;

    SECTION("Group files by format") {
        std::vector<std::filesystem::path> files = {
            "file1.pro",
            "file2.pro",
            "anim1.frm",
            "anim2.frm",
            "save.gam",
            "messages.msg"
        };

        auto groups = ReaderFactory::groupFilesByFormat(files);

        REQUIRE(groups[ReaderFactory::Format::PRO].size() == 2);
        REQUIRE(groups[ReaderFactory::Format::FRM].size() == 2);
        REQUIRE(groups[ReaderFactory::Format::GAM].size() == 1);
        REQUIRE(groups[ReaderFactory::Format::MSG].size() == 1);
    }
}