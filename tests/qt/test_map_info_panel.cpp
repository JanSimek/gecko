#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTreeWidget>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "format/map/Map.h"
#include "resource/GameResources.h"
#include "support/DataTree.h"
#include "support/MapObjectBuilder.h"
#include "support/QtDialogAnswer.h"
#include "support/TempFile.h"
#include "ui/Settings.h"
#include "ui/panels/MapInfoPanel.h"
#include "util/FileIo.h"

namespace test = geck::test;

// Phase A of the map-info editor work: the Map Info panel resolves the current map's friendly names
// (maps.txt lookup_name + the per-elevation map.msg display name) read-only, reusing MapNameResolver.
// test::mapOf leaves the header's own filename field ("newmap") unlike the file's, so the panel must
// resolve by the file basename (map->filename()), not header.filename.

TEST_CASE("MapInfoPanel names the map's own script from scripts.lst and scrname.msg", "[qt][mapinfo]") {
    test::DataTree data("ui-data");
    data.write("scripts/scripts.lst", "obj_dude.int    ; player\nzclrat.int      ; rat\n");
    // scriptDisplayName(programIndex 0) reads scrname.msg[0 + 101] = 101.
    data.write("text/english/game/scrname.msg", test::msgLine(101, "The Chosen One"));
    auto& resources = data.mount();

    auto settings = std::make_shared<geck::Settings>();
    auto map = test::mapOf("testmap.map");
    map->getMapFile().header.script_id = 1; // 1-based -> scripts.lst index 0 (obj_dude.int)

    geck::MapInfoPanel panel(resources, settings);
    panel.setMap(map.get());

    auto* scriptEdit = panel.findChild<QLineEdit*>("mapScript");
    REQUIRE(scriptEdit != nullptr);
    // .lst filename plus the scrname.msg description, resolved even with no .gam file mounted.
    CHECK(scriptEdit->text() == QStringLiteral("obj_dude.int — The Chosen One"));
}

TEST_CASE("MapInfoPanel shows resolved map.msg display name and maps.txt lookup name", "[qt][mapinfo]") {
    test::DataTree data("ui-data");
    data.write("data/maps.txt", "[Map 0]\nlookup_name=Test Town\nmap_name=testmap\n");
    // displayName(index 0, elevation 0) reads map.msg[0*3 + 0 + 200] = 200.
    data.write("text/english/game/map.msg", test::msgLine(200, "Test Display"));
    auto& resources = data.mount();

    auto settings = std::make_shared<geck::Settings>();
    auto map = test::mapOf("testmap.map");

    geck::MapInfoPanel panel(resources, settings);
    panel.setMap(map.get());

    auto* displayEdit = panel.findChild<QLineEdit*>("mapDisplayName");
    auto* lookupEdit = panel.findChild<QLineEdit*>("mapLookupName");
    REQUIRE(displayEdit != nullptr);
    REQUIRE(lookupEdit != nullptr);
    CHECK(displayEdit->text() == QStringLiteral("Test Display"));
    CHECK(lookupEdit->text() == QStringLiteral("Test Town"));

    // A map not listed in maps.txt resolves to a placeholder (read-only), not a crash.
    auto unknown = test::mapOf("nosuch.map");
    panel.setMap(unknown.get());
    CHECK(lookupEdit->text() == QStringLiteral("(not in maps.txt)"));
    CHECK(lookupEdit->isReadOnly());
}

TEST_CASE("MapInfoPanel persists edited map names to a writable Data Path and reflects them", "[qt][mapinfo]") {
    test::DataTree data("ui-data");
    data.write("data/maps.txt", "[Map 0]\nlookup_name=Test Town\nmap_name=testmap\n");
    data.write("text/english/game/map.msg", test::msgLine(200, "Test Display"));
    auto& resources = data.mount();

    // A writable folder, mounted after the source so its edited copy shadows the source.
    const test::TempDir writableDir("writable");
    const std::filesystem::path& writableRoot = writableDir.path();
    resources.files().addDataPath(writableRoot);

    auto settings = std::make_shared<geck::Settings>();
    settings->setDataPaths({ writableRoot }); // the writable folder IS the visible Data Path edits go to

    auto map = test::mapOf("testmap.map");
    geck::MapInfoPanel panel(resources, settings);
    panel.setMap(map.get());

    auto* lookupEdit = panel.findChild<QLineEdit*>("mapLookupName");
    auto* displayEdit = panel.findChild<QLineEdit*>("mapDisplayName");
    auto* hint = panel.findChild<QLabel*>("mapNamesOverlayHint");
    REQUIRE(lookupEdit != nullptr);
    REQUIRE(displayEdit != nullptr);
    REQUIRE(hint != nullptr);
    REQUIRE(lookupEdit->text() == QStringLiteral("Test Town"));
    CHECK(hint->isHidden()); // there is a writable Data Path -> no "add a folder" hint

    // Editing a name emits mapNamesChanged — the signal the main window turns into "map modified".
    QSignalSpy modifiedSpy(&panel, &geck::MapInfoPanel::mapNamesChanged);
    QTest::keyClicks(lookupEdit, "Z"); // user input fires textEdited -> mapNamesChanged
    CHECK(modifiedSpy.count() == 1);

    // Set the final values and persist — what the main window calls after writing the .map.
    lookupEdit->setText("Renamed Town");
    lookupEdit->setModified(true);
    displayEdit->setText("New Display");
    displayEdit->setModified(true);
    panel.persistMapNames();

    // Persisted: the writable Data Path carries both edits.
    const std::filesystem::path savedMaps = writableRoot / "data" / "maps.txt";
    const std::filesystem::path savedMsg = writableRoot / "text" / "english" / "game" / "map.msg";
    REQUIRE(std::filesystem::exists(savedMaps));
    REQUIRE(std::filesystem::exists(savedMsg));
    CHECK(geck::io::readFile(savedMaps).find("lookup_name=Renamed Town") != std::string::npos);
    CHECK(geck::io::readFile(savedMsg).find("{200}{}{New Display}") != std::string::npos);

    // Reflected: after the re-mount + resolver rebuild, the panel shows both new names.
    CHECK(lookupEdit->text() == QStringLiteral("Renamed Town"));
    CHECK(displayEdit->text() == QStringLiteral("New Display"));
    CHECK(hint->isHidden()); // still a writable Data Path -> still no hint
}

TEST_CASE("MapInfoPanel hints to add a writable Data Path when none is configured", "[qt][mapinfo]") {
    test::DataTree data("ui-data");
    data.write("data/maps.txt", "[Map 0]\nlookup_name=Test Town\nmap_name=testmap\n");
    data.write("text/english/game/map.msg", test::msgLine(200, "Test Display"));
    auto& resources = data.mount();

    auto settings = std::make_shared<geck::Settings>(); // no writable folder in the Data Paths

    auto map = test::mapOf("testmap.map");
    geck::MapInfoPanel panel(resources, settings);
    panel.setMap(map.get());

    auto* lookupEdit = panel.findChild<QLineEdit*>("mapLookupName");
    auto* hint = panel.findChild<QLabel*>("mapNamesOverlayHint");
    REQUIRE(lookupEdit != nullptr);
    REQUIRE(hint != nullptr);
    REQUIRE(lookupEdit->text() == QStringLiteral("Test Town")); // registered -> the name fields are editable

    // With no writable folder in Data Paths, the panel hints the user to add one instead of saving
    // anywhere hidden.
    CHECK_FALSE(hint->isHidden());
    CHECK(hint->text().contains("writable folder"));
}

namespace {

// Shared scaffolding for the global-variable tests: a map whose .gam is mounted, plus a writable Data
// Path mounted last so the edited .gam copy shadows the source. The values displayed AND edited are the
// .gam's MAP_GLOBAL_VARS values, since a BASE map's globals are re-read from the .gam by the engine,
// ignoring the .map's blocks.
struct GlobalVarFixture {
    test::DataTree data{ "map-globals" };
    test::TempDir writableDir{ "writable" };
    std::shared_ptr<geck::Settings> settings = std::make_shared<geck::Settings>();
    std::unique_ptr<geck::Map> map = test::mapOf("testmap.map");
    geck::resource::GameResources& resources;

    explicit GlobalVarFixture(const char* gamContents)
        : resources(mountData(data, gamContents)) {
        resources.files().addDataPath(writableDir.path());
        settings->setDataPaths({ writableDir.path() }); // the writable folder IS the visible Data Path edits go to
    }

    // Where persistMapVars() writes the map's .gam (its basename is the map filename's).
    std::filesystem::path savedGam() const { return writableDir.path() / "maps" / "testmap.gam"; }

private:
    static geck::resource::GameResources& mountData(test::DataTree& tree, const char* gamContents) {
        tree.write("scripts/scripts.lst", "obj_dude.int    ; player\n");
        tree.write("maps/testmap.gam", gamContents);
        return tree.mount();
    }
};

} // namespace

TEST_CASE("MapInfoPanel edits a global variable value and persists it to the map's .gam", "[qt][mapinfo]") {
    GlobalVarFixture fixture("MAP_GLOBAL_VARS:\nMVAR_first := 10;\nMVAR_second := 20;\n");
    auto& mapFile = fixture.map->getMapFile();
    // A real base map's stored block mirrors its .gam (the engine snapshots it on save); start aligned.
    mapFile.map_global_vars = { 10, 20 };
    mapFile.header.num_global_vars = 2;

    geck::MapInfoPanel panel(fixture.resources, fixture.settings);
    panel.setMap(fixture.map.get());

    auto* tree = panel.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);
    // Two variable rows; the count/source summary is a label under the tree, not a row in it.
    REQUIRE(tree->topLevelItemCount() == 2);

    QTreeWidgetItem* firstVar = tree->topLevelItem(0);
    REQUIRE(firstVar != nullptr);
    REQUIRE(firstVar->data(1, Qt::UserRole).isValid());
    CHECK(firstVar->data(1, Qt::UserRole).toInt() == 0);
    REQUIRE((firstVar->flags() & Qt::ItemIsEditable) != 0);
    CHECK(firstVar->text(1) == QStringLiteral("10")); // value shown is the .gam's MAP_GLOBAL_VARS value

    // The count/source summary is a label under the tree, not a row in it.
    auto* summaryLabel = panel.findChild<QLabel*>("globalVarsSummary");
    REQUIRE(summaryLabel != nullptr);
    CHECK(summaryLabel->text() == QStringLiteral("Total: 2 variables from testmap.gam"));

    QSignalSpy varSpy(&panel, &geck::MapInfoPanel::mapVariablesChanged);

    // Editing the first variable's Value cell emits the signal; nothing is written until persist.
    firstVar->setText(1, QStringLiteral("-42")); // negatives accepted (signed int32)
    CHECK(varSpy.count() == 1);

    // The edit is mirrored into the .map's stored block too, so the .map and .gam stay in sync.
    REQUIRE(mapFile.map_global_vars.size() == 2);
    CHECK(mapFile.map_global_vars[0] == -42);
    CHECK(mapFile.map_global_vars[1] == 20); // the other variable is untouched

    // Persisting writes the .gam to the writable Data Path, with the edited value and the other variable
    // untouched. A re-read reflects the edit.
    panel.persistMapVars();
    REQUIRE(std::filesystem::exists(fixture.savedGam()));
    const std::string saved = geck::io::readFile(fixture.savedGam());
    CHECK(saved.find("MVAR_first := -42;") != std::string::npos);
    CHECK(saved.find("MVAR_second := 20;") != std::string::npos); // the other variable is untouched
}

TEST_CASE("MapInfoPanel adds a map global variable via the name field and Add button", "[qt][mapinfo]") {
    GlobalVarFixture fixture("MAP_GLOBAL_VARS:\nMVAR_first := 10;\n");
    fixture.map->getMapFile().map_global_vars = { 10 }; // .map block aligned with the .gam
    fixture.map->getMapFile().header.num_global_vars = 1;

    geck::MapInfoPanel panel(fixture.resources, fixture.settings);
    panel.setMap(fixture.map.get());

    auto* nameEdit = panel.findChild<QLineEdit*>("newGlobalVarName");
    auto* addButton = panel.findChild<QPushButton*>("addGlobalVar");
    auto* tree = panel.findChild<QTreeWidget*>();
    REQUIRE(nameEdit != nullptr);
    REQUIRE(addButton != nullptr);
    REQUIRE(tree != nullptr);

    // One variable row before adding (the summary is a label, not a row).
    REQUIRE(tree->topLevelItemCount() == 1);

    QSignalSpy varSpy(&panel, &geck::MapInfoPanel::mapVariablesChanged);

    nameEdit->setText("MVAR_new");
    addButton->click();

    // The new (editable, value 0) row appears appended last, and the map is modified.
    REQUIRE(tree->topLevelItemCount() == 2);
    QTreeWidgetItem* added = tree->topLevelItem(1);
    REQUIRE(added != nullptr);
    CHECK(added->text(0) == QStringLiteral("MVAR_new"));
    CHECK(added->text(1) == QStringLiteral("0"));
    REQUIRE(added->data(1, Qt::UserRole).isValid());
    CHECK(added->data(1, Qt::UserRole).toInt() == 1); // appended last -> index 1
    CHECK((added->flags() & Qt::ItemIsEditable) != 0);
    CHECK(varSpy.count() == 1);
    CHECK(nameEdit->text().isEmpty()); // the field is cleared after a successful add

    // The .map's stored block grew in lockstep with the .gam.
    auto& addedMapFile = fixture.map->getMapFile();
    REQUIRE(addedMapFile.map_global_vars.size() == 2);
    CHECK(addedMapFile.map_global_vars[1] == 0);
    CHECK(addedMapFile.header.num_global_vars == 2);

    // Persisting writes the appended variable after the existing one.
    panel.persistMapVars();
    REQUIRE(std::filesystem::exists(fixture.savedGam()));
    const std::string out = geck::io::readFile(fixture.savedGam());
    CHECK(out.find("MVAR_first := 10;") != std::string::npos);
    CHECK(out.find("MVAR_new := 0;") != std::string::npos);
    CHECK(out.find("MVAR_first") < out.find("MVAR_new"));
}

TEST_CASE("MapInfoPanel removes the last map global variable after confirmation", "[qt][mapinfo]") {
    GlobalVarFixture fixture("MAP_GLOBAL_VARS:\nMVAR_first := 10;\nMVAR_last := 99;\n");
    fixture.map->getMapFile().map_global_vars = { 10, 99 }; // .map block aligned with the .gam
    fixture.map->getMapFile().header.num_global_vars = 2;

    geck::MapInfoPanel panel(fixture.resources, fixture.settings);
    panel.setMap(fixture.map.get());

    auto* tree = panel.findChild<QTreeWidget*>();
    auto* removeButton = panel.findChild<QPushButton*>("removeGlobalVar");
    REQUIRE(tree != nullptr);
    REQUIRE(removeButton != nullptr);
    REQUIRE(tree->topLevelItemCount() == 2); // two variables (the summary is a label)

    // Selecting the last variable row enables Remove.
    QTreeWidgetItem* lastVar = tree->topLevelItem(1);
    REQUIRE(lastVar != nullptr);
    REQUIRE(lastVar->data(1, Qt::UserRole).toInt() == 1);
    tree->setCurrentItem(lastVar);
    CHECK(removeButton->isEnabled());

    QSignalSpy varSpy(&panel, &geck::MapInfoPanel::mapVariablesChanged);

    // Confirm the warning -> the row is dropped, the survivor remains, and the map is modified.
    {
        const test::ScopedDialogAnswer confirm(QMessageBox::Yes);
        removeButton->click();
    }

    REQUIRE(tree->topLevelItemCount() == 1); // one variable (the summary is a label)
    QTreeWidgetItem* survivor = tree->topLevelItem(0);
    REQUIRE(survivor != nullptr);
    CHECK(survivor->text(0) == QStringLiteral("MVAR_first"));
    CHECK(varSpy.count() == 1);

    // The .map's stored block shrank in lockstep with the .gam.
    auto& removedMapFile = fixture.map->getMapFile();
    REQUIRE(removedMapFile.map_global_vars.size() == 1);
    CHECK(removedMapFile.map_global_vars[0] == 10);
    CHECK(removedMapFile.header.num_global_vars == 1);

    // The removed variable is gone from the persisted .gam.
    panel.persistMapVars();
    REQUIRE(std::filesystem::exists(fixture.savedGam()));
    const std::string out = geck::io::readFile(fixture.savedGam());
    CHECK(out.find("MVAR_first := 10;") != std::string::npos);
    CHECK(out.find("MVAR_last") == std::string::npos);
}

TEST_CASE("MapInfoPanel global-variable edits can be undone through the state it emits", "[qt][mapinfo][undo]") {
    GlobalVarFixture fixture("MAP_GLOBAL_VARS:\nMVAR_first := 10;\n");
    fixture.map->getMapFile().map_global_vars = { 10 };
    fixture.map->getMapFile().header.num_global_vars = 1;

    geck::MapInfoPanel panel(fixture.resources, fixture.settings);
    panel.setMap(fixture.map.get());
    auto* tree = panel.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);

    std::optional<geck::MapInfoPanel::GlobalVarsState> before;
    QObject::connect(&panel, &geck::MapInfoPanel::globalVarsEdited, &panel,
        [&before](const geck::MapInfoPanel::GlobalVarsState& b, const geck::MapInfoPanel::GlobalVarsState&, const QString&) {
            before = b;
        });

    QTreeWidgetItem* row = tree->topLevelItem(0);
    REQUIRE(row != nullptr);
    row->setText(1, QStringLiteral("7"));
    REQUIRE(before.has_value());
    CHECK(fixture.map->getMapFile().map_global_vars[0] == 7);

    // The host's undo applies the captured state: the .map block, the rows and the .gam come back.
    panel.applyGlobalVarsState(*before);
    CHECK(fixture.map->getMapFile().map_global_vars[0] == 10);
    // Applying a state rebuilds the rows, so look the row up again rather than reuse `row`.
    QTreeWidgetItem* restored = tree->topLevelItem(0);
    REQUIRE(restored != nullptr);
    CHECK(restored->text(1) == QStringLiteral("10"));
    panel.persistMapVars();
    CHECK(geck::io::readFile(fixture.savedGam()).find("MVAR_first := 10;") != std::string::npos);
}

TEST_CASE("MapInfoPanel requests header and elevation edits instead of writing the map", "[qt][mapinfo][undo]") {
    test::DataTree data("ui-data");
    data.write("scripts/scripts.lst", "obj_dude.int    ; player\n");
    auto& resources = data.mount();
    auto settings = std::make_shared<geck::Settings>();
    auto map = test::mapOf("testmap.map");
    const uint32_t originalPosition = map->getMapFile().header.player_default_position;

    geck::MapInfoPanel panel(resources, settings);
    panel.setMap(map.get());
    auto* positionSpin = panel.findChild<QSpinBox*>("playerPosition");
    auto* elevation2 = panel.findChild<QCheckBox*>("elevation2");
    REQUIRE(positionSpin != nullptr);
    REQUIRE(elevation2 != nullptr);
    REQUIRE(elevation2->isChecked());

    QSignalSpy headerSpy(&panel, &geck::MapInfoPanel::headerEditRequested);
    QSignalSpy elevationSpy(&panel, &geck::MapInfoPanel::elevationPresenceRequested);

    positionSpin->setValue(static_cast<int>(originalPosition) + 1);
    REQUIRE(headerSpy.count() == 1);
    CHECK(headerSpy.at(0).at(0).value<geck::Map::MapHeader>().player_default_position == originalPosition + 1);
    CHECK(headerSpy.at(0).at(2).toString() == QStringLiteral("playerPosition"));
    // Nothing applied the request, so the map and the widget still hold the original value.
    CHECK(map->getMapFile().header.player_default_position == originalPosition);
    CHECK(positionSpin->value() == static_cast<int>(originalPosition));

    {
        const test::ScopedDialogAnswer confirm(QMessageBox::Yes);
        elevation2->setChecked(false);
    }
    REQUIRE(elevationSpy.count() == 1);
    CHECK(elevationSpy.at(0).at(0).toInt() == 1);
    CHECK_FALSE(elevationSpy.at(0).at(1).toBool());
    CHECK(geck::Map::elevationIsPresent(map->getMapFile().header.flags, 1));
    CHECK(elevation2->isChecked());
}
