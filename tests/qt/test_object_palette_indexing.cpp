#include <catch2/catch_test_macros.hpp>

#include "resource/GameResources.h"
#include "support/Fixtures.h"
#include "ui/palette/PaletteModel.h"
#include "ui/panels/ObjectPalettePanel.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QListView>
#include <QMimeData>
#include <memory>

using namespace geck;

// The palette's fixture list is first.pro, broken.pro, third.pro. broken.pro cannot be parsed, so
// its entry is skipped and never takes a slot: the .lst line of third.pro is 2 while its position
// in the loaded list is 1. Everything that consumes a palette index has to agree on which of those
// it means, or dropping an object places the wrong one - or none.
TEST_CASE("Object palette indexes survive a proto that fails to load", "[qt][palette]") {
    auto resources = std::make_shared<resource::GameResources>();
    resources->files().addDataPath(geck::test::dataPath("palette"));

    ObjectPalettePanel panel(*resources);
    panel.loadObjects();
    QApplication::processEvents();

    auto* view = panel.findChild<QListView*>();
    REQUIRE(view != nullptr);
    const QAbstractItemModel* model = view->model();
    REQUIRE(model != nullptr);
    REQUIRE(model->rowCount() == 2); // the unreadable proto is not shown

    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        const int engineIndex = index.data(ui::PaletteModel::EngineIndexRole).toInt();

        // The index a row carries is the one getObjectInfo() takes - it is what the drag payload
        // holds and what placement resolves the proto through.
        const ObjectInfo* info = panel.getObjectInfo(engineIndex, ObjectCategory::ITEMS);
        REQUIRE(info != nullptr);
        CHECK(index.data(ui::PaletteModel::LabelRole).toString() == info->displayName);
    }

    SECTION("The last row resolves to the entry after the gap, not past the end") {
        const QModelIndex last = model->index(model->rowCount() - 1, 0);
        const int engineIndex = last.data(ui::PaletteModel::EngineIndexRole).toInt();

        const ObjectInfo* info = panel.getObjectInfo(engineIndex, ObjectCategory::ITEMS);
        REQUIRE(info != nullptr);
        CHECK(info->proFileName == QStringLiteral("third.pro"));
    }

    SECTION("The drag payload carries only the type the map reads") {
        const QModelIndex first = model->index(0, 0);
        std::unique_ptr<QMimeData> payload(model->mimeData({ first }));
        REQUIRE(payload != nullptr);

        CHECK(payload->hasFormat("application/x-geck-object"));
        // No plain text: with no drag pixmap the platform shows it beside the cursor.
        CHECK_FALSE(payload->hasText());
    }
}
