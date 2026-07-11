#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QToolBar>
#include <QWidget>

#include <memory>

#include "resource/GameResources.h"
#include "ui/Settings.h"
#include "ui/core/MainWindow.h"

using namespace geck;

TEST_CASE("MainWindow registers and removes plugin UI contributions", "[qt][plugins]") {
    auto resources = std::make_shared<resource::GameResources>();
    MainWindow window(resources, std::make_shared<Settings>());

    QAction* menuAction = window.addPluginMenuItem("plugin.menu", "Run Plugin");
    REQUIRE(menuAction != nullptr);
    CHECK(menuAction->text() == "Run Plugin");
    CHECK(window.addPluginMenuItem("plugin.menu", "Duplicate") == nullptr);

    QAction* toolAction = window.addPluginToolButton("plugin.tool", "Plugin Tool");
    REQUIRE(toolAction != nullptr);
    auto* toolbar = window.findChild<QToolBar*>("MainToolBar");
    REQUIRE(toolbar != nullptr);
    CHECK(toolbar->actions().contains(toolAction));

    auto* panel = new QWidget();
    QDockWidget* dock = window.addPluginDock("plugin.dock", "Plugin Dock", panel);
    REQUIRE(dock != nullptr);
    CHECK(dock->widget() == panel);
    auto duplicatePanel = std::make_unique<QWidget>();
    CHECK(window.addPluginDock("plugin.dock", "Duplicate Dock", duplicatePanel.get()) == nullptr);

    window.removePluginUi("plugin.tool");
    CHECK_FALSE(toolbar->actions().contains(toolAction));

    window.removePluginUi("plugin.menu");
    CHECK(window.addPluginMenuItem("plugin.menu", "Run Again") != nullptr);

    window.removePluginUi("plugin.dock");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(window.addPluginDock("plugin.dock", "Plugin Dock", new QWidget()) != nullptr);
}
