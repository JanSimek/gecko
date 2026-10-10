#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>

#include "resource/GameResources.h"
#include "ui/Settings.h"
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
    for (auto* section : reopened.findChildren<CollapsibleSection*>()) {
        CHECK(section->isExpanded() == (section->key() != "selection.script"));
    }
}
