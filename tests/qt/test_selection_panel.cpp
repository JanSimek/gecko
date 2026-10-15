#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "resource/GameResources.h"
#include "ui/Settings.h"
#include "ui/panels/ObjectScriptSection.h"
#include "ui/panels/SelectionPanel.h"
#include "ui/theme/ThemeManager.h"
#include "ui/widgets/CollapsibleSection.h"
#include "ui/widgets/ElidedLabel.h"

using namespace geck;

namespace {

// Space the panel needs around its scrolled content: its own margins, the scroll area's frame and a
// vertical scroll bar.
int panelChrome(const SelectionPanel& panel) {
    const auto* scrollArea = panel.findChild<QScrollArea*>();
    REQUIRE(scrollArea != nullptr);
    return 2 * ui::constants::PANEL_CONTENT_MARGIN + 2 * scrollArea->frameWidth()
        + panel.style()->pixelMetric(QStyle::PM_ScrollBarExtent);
}

} // namespace

// The object view used to need 341-368 px - a fixed preview column beside a form of read-only line
// edits and button rows - so the default 360 px dock scrolled sideways and had to be widened by hand.
// Show every section an object can have, fill every value with text far wider than the dock, and the
// panel must still fit: values shorten, warnings wrap, nothing sets a minimum from its content.
TEST_CASE("Selection panel's object view fits the default dock width whatever it shows", "[ui][selection]") {
    resource::GameResources resources;
    SelectionPanel panel(resources);

    const QString wide(400, QChar('W'));
    for (auto* section : panel.findChildren<CollapsibleSection*>()) {
        section->setVisible(true);
        section->setExpanded(true);
        for (auto* button : section->findChildren<QPushButton*>()) {
            button->setVisible(true); // the script actions show only when they apply
        }
    }
    for (auto* label : panel.findChildren<ElidedLabel*>()) {
        label->setFullText(wide);
        label->setVisible(true);
    }
    for (auto* label : panel.findChildren<QLabel*>()) {
        if (label->wordWrap()) {
            label->setText("A warning sentence that keeps going " + QString("and going ").repeated(30));
            label->setVisible(true);
        }
    }

    const auto* scrollArea = panel.findChild<QScrollArea*>();
    REQUIRE(scrollArea != nullptr);
    const auto* stack = scrollArea->widget()->findChild<QStackedWidget*>();
    REQUIRE(stack != nullptr);
    REQUIRE(stack->currentIndex() == 0); // the object page shows when nothing is selected

    const int needed = scrollArea->widget()->minimumSizeHint().width() + panelChrome(panel);
    INFO("object view needs " << needed << " px");
    CHECK(needed <= ui::constants::sizes::PANEL_PREFERRED_WIDTH);
}

TEST_CASE("Selection panel remembers which sections are collapsed", "[ui][selection]") {
    resource::GameResources resources;
    auto settings = std::make_shared<Settings>();
    settings->setCollapsedSections({});

    {
        SelectionPanel panel(resources, settings);
        CollapsibleSection* script = nullptr;
        for (auto* section : panel.findChildren<CollapsibleSection*>()) {
            if (section->key() == "selection.script") {
                script = section;
            }
        }
        REQUIRE(script != nullptr);
        REQUIRE(script->isExpanded());

        // Fold it the way a click does: the header button toggles and the section reports it.
        auto* header = script->findChild<QToolButton*>();
        REQUIRE(header != nullptr);
        header->click();
        CHECK_FALSE(script->isExpanded());
        CHECK(settings->getCollapsedSections() == QStringList{ "selection.script" });
    }

    SelectionPanel reopened(resources, settings);
    for (const auto* section : reopened.findChildren<CollapsibleSection*>()) {
        CHECK(section->isExpanded() == (section->key() != "selection.script"));
    }
}

// The Script section reports what the script's parsed source does wrong (resource::ScriptLinter) and
// when the map gives the placed script fewer local variables than scripts.lst does.
TEST_CASE("Script section shows the script's lint findings and a short local variable block", "[ui][selection]") {
    resource::GameResources resources;
    Map map("testmap.map");
    map.setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));
    auto script = MapScript::makeObjectScript(MapScript::ScriptType::CRITTER, 0, 0, 42);
    script.local_var_offset = 0;
    script.local_var_count = 2;
    map.getMapFile().map_scripts[static_cast<int>(MapScript::ScriptType::CRITTER)].push_back(script);
    map.getMapFile().map_local_vars = { 0, 0 };

    auto critter = std::make_shared<MapObject>();
    critter->pro_pid = 0x01000003;
    critter->map_scripts_pid = static_cast<int32_t>(script.pid);
    critter->ai_packet = 1;

    ObjectScriptSection section(resources);
    section.setStatusProvider([](int) {
        ScriptSourceService::ScriptStatus status;
        status.resolved = true;
        status.compiled = true;
        status.localVarCount = 4;
        auto report = std::make_shared<resource::ScriptReport>();
        report->script = "src/one.ssl";
        ssl::ScriptFacts::Override ai;
        ai.field = ssl::ScriptFacts::Override::Field::AiPacket;
        ai.procedure = "map_enter_p_proc";
        ai.file = "src/one.ssl";
        ai.line = 12;
        ai.value = 2;
        ai.valueName = "AI_CALM";
        report->overrides.push_back(ai);
        status.report = report;
        status.findings.push_back({ resource::ScriptFinding::Severity::Error, "messages", "message 150 is not in dialog/one.msg",
            { "src/headers/shared.h", 30 }, "talk_p_proc" });
        return status;
    });
    section.showObject(&map, critter);

    QString shown;
    for (const auto* label : section.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&section)) {
            shown += label->text() + "\n";
        }
    }
    INFO(shown.toStdString());
    CHECK(shown.contains("Sets the AI packet to AI_CALM (2) on map entry (map_enter_p_proc, line 12), replacing the map's 1"));
    CHECK(shown.contains("shared.h line 30: message 150 is not in dialog/one.msg"));
    CHECK(shown.contains("The map gives this script 2 local variables, but scripts.lst gives it 4"));
}
