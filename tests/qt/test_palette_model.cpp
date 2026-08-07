#include <catch2/catch_test_macros.hpp>

#include "ui/palette/PaletteModel.h"

#include <QPixmap>

using geck::ui::PaletteItem;
using geck::ui::PaletteModel;

TEST_CASE("PaletteModel serves rows to a view", "[qt][palette]") {
    PaletteModel model;
    model.setItems({ { 7, "grass", "Tile #7" }, { 12, "sand", "Tile #12" } });

    REQUIRE(model.rowCount() == 2);
    CHECK(model.data(model.index(0, 0), Qt::DisplayRole).toString() == "grass");
    CHECK(model.data(model.index(1, 0), Qt::ToolTipRole).toString() == "Tile #12");
    CHECK(model.data(model.index(1, 0), PaletteModel::EngineIndexRole).toInt() == 12);

    SECTION("The engine index is what a panel selects by, not the row") {
        CHECK(model.rowForEngineIndex(12) == 1);
        CHECK(model.rowForEngineIndex(999) == -1);
    }

    SECTION("Icons are produced on demand and only once per row") {
        int calls = 0;
        model.setIconProvider([&calls](const PaletteItem&) {
            ++calls;
            return QPixmap(4, 4);
        });

        CHECK(calls == 0); // nothing painted yet, nothing built

        (void)model.data(model.index(0, 0), Qt::DecorationRole);
        (void)model.data(model.index(0, 0), Qt::DecorationRole);
        CHECK(calls == 1); // second ask is served from the cache

        (void)model.data(model.index(1, 0), Qt::DecorationRole);
        CHECK(calls == 2);
    }

    SECTION("Replacing the items drops the cached icons") {
        int calls = 0;
        model.setIconProvider([&calls](const PaletteItem&) {
            ++calls;
            return QPixmap(4, 4);
        });
        (void)model.data(model.index(0, 0), Qt::DecorationRole);
        REQUIRE(calls == 1);

        model.setItems({ { 3, "rock", "Tile #3" } });
        (void)model.data(model.index(0, 0), Qt::DecorationRole);
        CHECK(calls == 2);
    }

    SECTION("An out-of-range row yields nothing rather than misreporting") {
        CHECK(model.itemAt(5) == nullptr);
        CHECK_FALSE(model.data(model.index(5, 0), Qt::DisplayRole).isValid());
    }
}
