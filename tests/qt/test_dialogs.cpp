#include <catch2/catch_test_macros.hpp>

#include <QChar>
#include <QComboBox>
#include <QString>
#include <QTableWidget>
#include <QTreeWidget>

#include <cstdint>
#include <vector>

#include "format/ObjTypes.h"
#include "format/ai/AiPacket.h"
#include "format/pro/ProtoTypes.h"
#include "support/DataTree.h"
#include "support/QtFind.h"
#include "ui/dialogs/CritterPropertiesDialog.h"
#include "ui/dialogs/FrmSelectorDialog.h"
#include "ui/dialogs/ItemSelectorDialog.h"
#include "ui/dialogs/LightPropertiesDialog.h"
#include "ui/dialogs/ScriptSelectorDialog.h"

namespace test = geck::test;

TEST_CASE("ScriptSelectorDialog shows index/filename/name/comment columns and returns the program index", "[script_selector_dialog]") {
    const std::vector<geck::ScriptSelectorDialog::Entry> entries = {
        { 0, "obj_dude.int", "The Chosen One", "Player script." },
        { 1, "obj_door.int", "", "A door" },
        { 2, "raiders.int", "Raiders", "" },
    };
    geck::ScriptSelectorDialog dialog(entries, /*currentIndex=*/2);
    auto* table = dialog.findChild<QTableWidget*>();
    REQUIRE(table != nullptr);
    REQUIRE(table->rowCount() == 3);
    REQUIRE(table->columnCount() == 4);

    // Find a row by its filename so the assertions don't depend on the (sortable) row order. Columns are
    // index / filename / name / comment.
    int dudeRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (test::requireItem(*table, row, 1).text() == "obj_dude.int") {
            dudeRow = row;
            break;
        }
    }
    REQUIRE(dudeRow >= 0);
    CHECK(test::requireItem(*table, dudeRow, 0).data(Qt::DisplayRole).toInt() == 0);
    CHECK(test::requireItem(*table, dudeRow, 2).text() == "The Chosen One");
    CHECK(test::requireItem(*table, dudeRow, 3).text() == "Player script."); // scripts.lst comment column

    // currentIndex == 2 is preselected; selectedIndex returns the program index, not the row.
    CHECK(dialog.selectedIndex() == 2);
    table->selectRow(dudeRow);
    CHECK(dialog.selectedIndex() == 0);
}

TEST_CASE("CritterPropertiesDialog names the AI packet from ai.txt and falls back to the raw number", "[critter_properties_dialog]") {
    geck::AiTxt aiTxt;
    aiTxt.add(geck::AiPacket{ .packetNum = 5, .name = "Coward" });
    aiTxt.add(geck::AiPacket{ .packetNum = 21, .name = "Berserker" });
    const QString aiPacketTip = "AI behaviour packet: pick by name (from the game's ai.txt) or type a packet number.";

    SECTION("current packet is in ai.txt -> shown by name, value round-trips") {
        geck::CritterPropertiesDialog dialog(21, 1, 100, 0, 0, aiTxt);
        auto* combo = test::findByToolTip<QComboBox>(dialog, aiPacketTip);
        REQUIRE(combo != nullptr);
        CHECK(combo->count() == 2); // the two packets, no raw fallback entry
        CHECK(combo->currentText().contains("Berserker"));
        CHECK(dialog.getAiPacket() == 21u);
    }

    SECTION("current packet not in ai.txt -> a raw fallback entry preserves the value") {
        geck::CritterPropertiesDialog dialog(99, 1, 100, 0, 0, aiTxt);
        auto* combo = test::findByToolTip<QComboBox>(dialog, aiPacketTip);
        REQUIRE(combo != nullptr);
        CHECK(combo->count() == 3); // two packets + a raw "Packet 99" entry
        CHECK(combo->currentText().contains("99"));
        CHECK(dialog.getAiPacket() == 99u);
    }

    SECTION("an arbitrary number can be typed into the editable combo") {
        geck::CritterPropertiesDialog dialog(5, 1, 100, 0, 0, aiTxt);
        auto* combo = test::findByToolTip<QComboBox>(dialog, aiPacketTip);
        REQUIRE(combo != nullptr);
        REQUIRE(combo->isEditable());
        combo->setCurrentText("150"); // not a named packet -> a freely-typed raw number
        CHECK(dialog.getAiPacket() == 150u);
    }
}

TEST_CASE("ItemSelectorDialog lists items.lst entries by their item PID", "[item_selector_dialog]") {
    test::DataTree data("item-selector");
    data.write("proto/items/items.lst", "aaa.pro\nbbb.pro\nccc.pro\n");
    auto& resources = data.mount();

    geck::ItemSelectorDialog dialog(resources);
    auto* tree = dialog.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);
    REQUIRE(tree->topLevelItemCount() == 3);

    // No .pro files are mounted, so describeItem cannot resolve a name; the dialog falls back to the
    // .pro filename and shows the item PID = ProtoId(ObjectType::Item, 1-based items.lst line) in the PID column.
    struct Expect {
        const char* name;
        uint32_t pid;
    };
    const Expect expected[] = {
        { "aaa.pro", geck::ProtoId(geck::ObjectType::Item, 1).pid() },
        { "bbb.pro", geck::ProtoId(geck::ObjectType::Item, 2).pid() },
        { "ccc.pro", geck::ProtoId(geck::ObjectType::Item, 3).pid() },
    };
    for (int i = 0; i < 3; ++i) {
        QTreeWidgetItem* row = tree->topLevelItem(i); // sorted by name -> aaa, bbb, ccc
        const QString expectedPid = QString("0x%1").arg(expected[i].pid, 8, 16, QChar('0'));
        CHECK(row->text(0).toStdString() == expected[i].name);          // Name column (filename fallback)
        CHECK(row->text(2).toStdString() == expectedPid.toStdString()); // PID column (visible)
    }

    // Nothing chosen yet; the amount defaults to 1.
    CHECK_FALSE(dialog.selectedPid().has_value());
    CHECK(dialog.selectedAmount() == 1);

    // Selecting a row exposes that row's PID through the public API.
    tree->setCurrentItem(tree->topLevelItem(0));
    REQUIRE(dialog.selectedPid().has_value());
    CHECK(dialog.selectedPid().value() == expected[0].pid);
}

TEST_CASE("FRM selector filters a rotated critter FID by its type nibble", "[frm_selector_dialog]") {
    // A critter FID carries rotation bits above the type nibble; reading the whole high byte made
    // 0x11000001 look like type 0x11 and dropped the critter filter.
    CHECK(geck::FrmSelectorDialog::filterForFid(0x11000001u) == geck::ObjectType::Critter);
    CHECK(geck::FrmSelectorDialog::filterForFid(0x02000015u) == geck::ObjectType::Scenery);
    CHECK_FALSE(geck::FrmSelectorDialog::filterForFid(0x06000003u).has_value()); // interface art
}

// The dialog edits whole percents of the engine's 65536. Truncating both ways stored 10% as 6553,
// which read back as 9%; every percent must survive a save and reopen.
TEST_CASE("Light dialog keeps the intensity percent it stored", "[light_properties_dialog]") {
    for (uint32_t percent = 0; percent <= 100; ++percent) {
        const uint32_t raw = (percent * 0x10000u + 50) / 100;
        geck::LightPropertiesDialog dialog(4, raw);
        INFO("percent " << percent);
        CHECK(dialog.getLightIntensity() == raw);
    }
    // fallout2-ce's mapper truncates (mp_instance.cc), so its maps store 10% as 6553.
    geck::LightPropertiesDialog truncated(4, 6553);
    CHECK(truncated.getLightIntensity() == 6554);
}
