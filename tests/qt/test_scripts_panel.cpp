#include <catch2/catch_test_macros.hpp>

#include <QLineEdit>
#include <QSignalSpy>
#include <QTableWidget>

#include <algorithm>
#include <memory>
#include <vector>

#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "resource/ScriptLint.h"
#include "support/CompiledScript.h"
#include "support/DataTree.h"
#include "support/MapObjectBuilder.h"
#include "support/QtFind.h"
#include "support/QtWait.h"
#include "ui/ExternalEditorLauncher.h"
#include "ui/ScriptSourceService.h"
#include "ui/Settings.h"
#include "ui/panels/ScriptsPanel.h"

namespace test = geck::test;

TEST_CASE("ScriptsPanel lists the map's scripts with resolved filename and name", "[scripts_panel]") {
    test::DataTree data("ui-data");
    data.write("scripts/scripts.lst", "obj_dude.int    ; player\nzclrat.int      ; rat\n");
    // scriptDisplayName(programIndex 0) reads scrname.msg[0 + 101] = 101.
    data.write("text/english/game/scrname.msg", test::msgLine(101, "The Chosen One"));
    auto& resources = data.mount();

    auto map = test::mapOf("testmap.map");

    // One CRITTER-section script (program index 0 -> obj_dude.int) owned by object 42, plus a second one
    // so the row count reflects every section's scripts.
    auto& mapFile = map->getMapFile();
    mapFile.map_scripts[static_cast<int>(geck::MapScript::ScriptType::CRITTER)].push_back(
        geck::MapScript::makeObjectScript(geck::MapScript::ScriptType::CRITTER, 0, 0, 42));
    mapFile.map_scripts[static_cast<int>(geck::MapScript::ScriptType::ITEM)].push_back(
        geck::MapScript::makeObjectScript(geck::MapScript::ScriptType::ITEM, 1, 1, 7));

    // The map's own (header) script is 1-based; script_id 1 resolves to scripts.lst index 0
    // (obj_dude.int) and is listed as a separate "Map" row.
    mapFile.header.script_id = 1;

    geck::ScriptsPanel panel(resources);
    panel.setMap(map.get());

    auto* table = panel.findChild<QTableWidget*>("scriptsTable"); // the script list (not the local-vars table)
    REQUIRE(table != nullptr);
    CHECK(table->rowCount() == 3); // 2 section scripts + the map's own header script

    // Rows are found by their section (sorting may reorder them). The critter script (program index
    // 0) and the map's own script (script_id 1 -> scripts.lst index 0) both resolve to obj_dude.int /
    // "The Chosen One"; the map's is listed as a distinct "Map"-section row.
    const auto sectionRow = [table](const QString& section) {
        return test::findTableRow(*table, 0, [&section](const QTableWidgetItem& item) { return item.text() == section; }); // COL_SECTION
    };
    const int critterRow = sectionRow(QStringLiteral("Critter"));
    const int mapRow = sectionRow(QStringLiteral("Map"));
    REQUIRE(critterRow >= 0);
    REQUIRE(mapRow >= 0);
    for (const int row : { critterRow, mapRow }) {
        CHECK(test::requireItem(*table, row, 1).data(Qt::DisplayRole).toInt() == 0);         // COL_SCRIPT_ID (0-based)
        CHECK(test::requireItem(*table, row, 2).text() == QStringLiteral("obj_dude.int"));   // COL_FILENAME
        CHECK(test::requireItem(*table, row, 3).text() == QStringLiteral("The Chosen One")); // COL_NAME
    }

    // Double-clicking an object-owned row emits scriptObjectActivated carrying that row's SID (stored in
    // the COL_SCRIPT_ID UserRole); double-clicking the ownerless "Map" row emits nothing.
    QSignalSpy activatedSpy(&panel, &geck::ScriptsPanel::scriptObjectActivated);
    const qulonglong critterSid = test::requireItem(*table, critterRow, 1).data(Qt::UserRole).toULongLong(); // COL_SCRIPT_ID
    CHECK(critterSid != static_cast<qulonglong>(geck::MapScript::NONE));                                     // owned row carries a real SID

    Q_EMIT table->cellDoubleClicked(critterRow, 1);
    REQUIRE(activatedSpy.count() == 1);
    CHECK(activatedSpy.takeFirst().at(0).toInt() == static_cast<int>(critterSid));

    Q_EMIT table->cellDoubleClicked(mapRow, 0); // ownerless map row -> no navigation
    CHECK(activatedSpy.count() == 0);

    // Regression: an active filter is re-applied after the table is re-populated (e.g. a map switch),
    // rather than silently showing every row again.
    auto* filter = panel.findChild<QLineEdit*>();
    REQUIRE(filter != nullptr);
    filter->setText("obj_dude"); // matches the critter section row and the Map header-script row
    panel.setMap(map.get());     // re-populate; the filter must still be honoured
    int visibleRows = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->isRowHidden(row)) {
            ++visibleRows;
        }
    }
    CHECK(visibleRows == 2); // both the section script and the map's own script resolve to obj_dude.int
    filter->clear();

    // Clearing the map empties the table without crashing.
    panel.setMap(nullptr);
    CHECK(table->rowCount() == 0);
}

TEST_CASE("ScriptsPanel shows the selected script's local variables", "[scripts_panel]") {
    test::DataTree data("ui-data");
    data.write("scripts/scripts.lst", "obj_dude.int    ; player\n");
    auto& resources = data.mount();

    auto map = test::mapOf("testmap.map");
    auto& mapFile = map->getMapFile();

    // A critter script whose two local variables sit at offset 0 in the map's flat LVAR array.
    auto script = geck::MapScript::makeObjectScript(geck::MapScript::ScriptType::CRITTER, 0, 0, 42);
    script.local_var_offset = 0;
    script.local_var_count = 2;
    mapFile.map_scripts[static_cast<int>(geck::MapScript::ScriptType::CRITTER)].push_back(script);
    mapFile.map_local_vars = { 111, 222 };

    geck::ScriptsPanel panel(resources);
    panel.setMap(map.get());

    auto* scriptsTable = panel.findChild<QTableWidget*>("scriptsTable");
    auto* lvarTable = panel.findChild<QTableWidget*>("localVarsTable");
    REQUIRE(scriptsTable != nullptr);
    REQUIRE(lvarTable != nullptr);
    REQUIRE(scriptsTable->rowCount() == 1);
    CHECK(lvarTable->rowCount() == 0); // nothing selected yet

    scriptsTable->selectRow(0); // selecting the script reveals its local variables
    REQUIRE(lvarTable->rowCount() == 2);
    CHECK(test::requireItem(*lvarTable, 0, 1).data(Qt::DisplayRole).toInt() == 111); // column 1 = Value
    CHECK(test::requireItem(*lvarTable, 1, 1).data(Qt::DisplayRole).toInt() == 222);
}

namespace {

// The Filename cell of the scripts-table row for `programIndex` (sorting may reorder rows), or nullptr.
QTableWidgetItem* scriptFilenameCell(const QTableWidget& table, int programIndex) {
    const int row = test::findTableRow(table, 1, [programIndex](const QTableWidgetItem& item) { // COL_SCRIPT_ID
        return item.data(Qt::DisplayRole).toInt() == programIndex;
    });
    return row >= 0 ? table.item(row, 2) : nullptr; // COL_FILENAME
}

} // namespace

// After the table fills, each script is linted (one per event-loop turn) and its rows get a marker:
// the lint's findings, and a placed script whose local variable block overlaps another's.
TEST_CASE("ScriptsPanel marks scripts the lint finds problems in", "[scripts_panel]") {
    test::DataTree data("ui-data");
    data.write("scripts/scripts.lst", "obj_dude.int    ; player # local_vars=4\nclean.int ; fine # local_vars=1\n");
    auto& resources = data.mount();

    auto map = test::mapOf("testmap.map");
    auto& mapFile = map->getMapFile();
    auto dude = geck::MapScript::makeObjectScript(geck::MapScript::ScriptType::CRITTER, 0, 0, 42);
    dude.local_var_offset = 0; // 0-3
    mapFile.map_scripts[static_cast<int>(geck::MapScript::ScriptType::CRITTER)].push_back(dude);
    auto clean = geck::MapScript::makeObjectScript(geck::MapScript::ScriptType::ITEM, 1, 1, 7);
    clean.local_var_offset = 3; // inside obj_dude's block, but clean.int never uses its variable
    mapFile.map_scripts[static_cast<int>(geck::MapScript::ScriptType::ITEM)].push_back(clean);
    mapFile.map_local_vars = { 0, 0, 0, 0 };

    std::vector<int> linted; // program indices the panel has asked about, in order
    geck::ScriptsPanel panel(resources);
    panel.setStatusProvider([&linted](int programIndex) {
        linted.push_back(programIndex);
        geck::ScriptSourceService::ScriptStatus status;
        status.resolved = true;
        auto report = std::make_shared<geck::resource::ScriptReport>();
        if (programIndex == 0) {
            report->locals.push_back({ geck::ssl::VariableRef{}, { "scripts/obj_dude.ssl", 20 } }); // uses its variables
        }
        status.report = report;
        if (programIndex == 0) {
            status.findings.push_back({ geck::resource::ScriptFinding::Severity::Error, "messages", "message 150 is not in dialog/obj_dude.msg",
                { "scripts/obj_dude.ssl", 12 }, "talk_p_proc" });
        }
        return status;
    });
    panel.setMap(map.get());

    auto* table = panel.findChild<QTableWidget*>("scriptsTable");
    REQUIRE(table != nullptr);
    // The provider returns into markRows, so once it has seen script 1 both rows are marked.
    REQUIRE(geck::test::pumpUntil([&linted]() { return std::ranges::find(linted, 1) != linted.end(); }));
    REQUIRE(scriptFilenameCell(*table, 0) != nullptr);
    const QString tip = scriptFilenameCell(*table, 0)->toolTip();
    CHECK(tip.contains("clean.int")); // the overlap finding names the other script (exact text: test_script_lint)
    CHECK(tip.contains("obj_dude.ssl line 12: message 150 is not in dialog/obj_dude.msg"));
    CHECK_FALSE(scriptFilenameCell(*table, 0)->icon().isNull());
    REQUIRE(scriptFilenameCell(*table, 1) != nullptr);
    CHECK(scriptFilenameCell(*table, 1)->toolTip().isEmpty()); // nothing wrong with the clean script
    CHECK(scriptFilenameCell(*table, 1)->icon().isNull());
}

// The marker also checks each script against where the map places it: the map's own script may not
// use local variables, and a spatial script needs spatial_p_proc.
TEST_CASE("ScriptsPanel marks scripts wrong for where the map places them", "[scripts_panel]") {
    test::DataTree data("ui-data");
    data.write("scripts/scripts.lst", "mapscr.int ; map # local_vars=1\nspot.int ; trigger # local_vars=0\n");
    auto& resources = data.mount();

    auto map = test::mapOf("testmap.map");
    auto& mapFile = map->getMapFile();
    mapFile.header.script_id = 1; // 1-based: mapscr.int
    mapFile.map_scripts[static_cast<int>(geck::MapScript::ScriptType::SPATIAL)].push_back(
        geck::MapScript::makeObjectScript(geck::MapScript::ScriptType::SPATIAL, 1, 1, 0)); // spot.int

    geck::ScriptsPanel panel(resources);
    panel.setStatusProvider([](int programIndex) {
        geck::ScriptSourceService::ScriptStatus status;
        status.resolved = true;
        auto report = std::make_shared<geck::resource::ScriptReport>();
        report->script = programIndex == 0 ? "scripts/mapscr.ssl" : "scripts/spot.ssl";
        if (programIndex == 0) {
            report->locals.push_back({ geck::ssl::VariableRef{}, { "scripts/mapscr.ssl", 30 } });
        }
        status.report = report;
        return status;
    });
    panel.setMap(map.get());

    auto* table = panel.findChild<QTableWidget*>("scriptsTable");
    REQUIRE(table != nullptr);
    REQUIRE(geck::test::pumpUntil([table]() { return scriptFilenameCell(*table, 0) != nullptr && !scriptFilenameCell(*table, 0)->toolTip().isEmpty(); }));
    CHECK(scriptFilenameCell(*table, 0)->toolTip().contains("mapscr.ssl line 30: This is a system script"));
    REQUIRE(geck::test::pumpUntil([table]() { return scriptFilenameCell(*table, 1) != nullptr && !scriptFilenameCell(*table, 1)->toolTip().isEmpty(); }));
    CHECK(scriptFilenameCell(*table, 1)->toolTip().contains("A spatial script without spatial_p_proc"));
}

// Without an .ssl source the status comes from the compiled .int, so a vanilla install is checked too.
// What the bytecode lint finds is covered by test_script_lint; this checks the service hands it on.
TEST_CASE("ScriptSourceService checks a script's compiled .int when it has no source", "[script_source_service]") {
    test::DataTree data("ui-data");
    data.write("scripts/scripts.lst", "two.int ; second # local_vars=0\n");
    geck::test::ByteWriter code;
    geck::test::pushInt(code, 0);
    geck::test::pushInt(code, 1);
    code.be16(0x80C2); // op_set_local_var: local variable 0, past the 0 scripts.lst gives
    data.writeBytes("scripts/two.int", geck::test::compiledScript("talk_p_proc", code.data()));
    auto& resources = data.mount();

    auto settings = std::make_shared<geck::Settings>();
    geck::ExternalEditorLauncher launcher(resources, settings, nullptr);
    geck::ScriptSourceService service(resources, settings, launcher, nullptr);
    const auto status = service.scriptStatus(0);
    CHECK(status.compiled);
    REQUIRE(status.report != nullptr);
    CHECK(status.report->fromBytecode);
    CHECK(status.findings.size() == 1);
}
