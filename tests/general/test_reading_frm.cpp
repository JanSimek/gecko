#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "format/frm/Direction.h"
#include "format/frm/Frame.h"
#include "format/frm/Frm.h"
#include "reader/frm/FrmReader.h"
#include "support/ByteWriter.h"
#include "support/Fixtures.h"

using namespace geck;

TEST_CASE("FrmReader parses the real flower.frm fixture", "[frm]") {
    FrmReader reader;
    auto frm = reader.openFile(geck::test::dataPath("flower.frm"));

    REQUIRE(frm != nullptr);
    CHECK(frm->version() == 4);
    CHECK(frm->fps() == 1);
    CHECK(frm->actionFrame() == 0);
    CHECK(frm->framesPerDirection() == 1);

    // All six directions share data offset 0, so the duplicates collapse to one.
    REQUIRE(frm->directions().size() == 1);
    const auto& dir = frm->directions()[0];
    CHECK(dir.shiftX() == 0);
    CHECK(dir.shiftY() == 2);

    REQUIRE(dir.frames().size() == 1);
    const auto& frame = dir.frames()[0];
    CHECK(frame.width() == 9);
    CHECK(frame.height() == 6);
    CHECK(frame.offsetX() == 0);
    CHECK(frame.offsetY() == 0);
}

TEST_CASE("FrmReader reads the header, per-direction shifts, frame offsets and pixels", "[frm]") {
    geck::test::ByteWriter w;
    w.be32(4)     // version
        .be16(5)  // fps
        .be16(2)  // actionFrame
        .be16(1); // framesPerDirection
    for (int i = 0; i < Frm::DIRECTIONS; ++i) {
        w.be16(11); // shiftX (per direction)
    }
    for (int i = 0; i < Frm::DIRECTIONS; ++i) {
        w.be16(22); // shiftY (per direction)
    }
    for (int i = 0; i < Frm::DIRECTIONS; ++i) {
        w.be32(0); // dataOffset (all equal -> collapses to one direction)
    }
    w.be32(16);  // size_of_frame_data: one 2x2 frame (12-byte header + 4 pixels)
    w.be16(2)    // width
        .be16(2) // height
        .be32(4) // pixel_count (width * height)
        .be16(3) // offsetX
        .be16(4) // offsetY
        .u8(0x10)
        .u8(0x20)
        .u8(0x30)
        .u8(0x40); // 2x2 pixel indices, row-major

    FrmReader reader;
    auto frm = reader.openFile("synthetic.frm", w.data());

    REQUIRE(frm != nullptr);
    CHECK(frm->version() == 4);
    CHECK(frm->fps() == 5);
    CHECK(frm->actionFrame() == 2);
    CHECK(frm->framesPerDirection() == 1);

    REQUIRE(frm->directions().size() == 1);
    const auto& dir = frm->directions()[0];
    CHECK(dir.shiftX() == 11);
    CHECK(dir.shiftY() == 22);

    REQUIRE(dir.frames().size() == 1);
    const auto& frame = dir.frames()[0];
    CHECK(frame.width() == 2);
    CHECK(frame.height() == 2);
    CHECK(frame.offsetX() == 3);
    CHECK(frame.offsetY() == 4);
    CHECK(frame.index(0, 0) == 0x10);
    CHECK(frame.index(1, 0) == 0x20);
    CHECK(frame.index(0, 1) == 0x30);
    CHECK(frame.index(1, 1) == 0x40);
}

TEST_CASE("FrmReader rejects an unsupported version", "[frm]") {
    geck::test::ByteWriter w;
    w.be32(3);     // version != 4
    w.fill(0, 80); // pad past the 62-byte minimum header

    FrmReader reader;
    REQUIRE_THROWS(reader.openFile("badversion.frm", w.data()));
}
