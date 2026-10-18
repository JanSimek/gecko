#pragma once

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMenu>
#include <QString>
#include <QTest>

#include <memory>
#include <utility>

#include "format/map/Map.h"
#include "resource/GameResources.h"
#include "support/QtFind.h"
#include "ui/Settings.h"
#include "ui/core/EditorWidget.h"
#include "ui/core/MainWindow.h"

namespace geck::test {

/// A shown and exposed MainWindow over `resources` (none mounted by default; pass
/// editorResources() to open a map) with default Settings.
struct MainWindowFixture {
    std::shared_ptr<resource::GameResources> resources;
    std::shared_ptr<Settings> settings = std::make_shared<Settings>();
    MainWindow window;

    explicit MainWindowFixture(std::shared_ptr<resource::GameResources> gameResources = std::make_shared<resource::GameResources>())
        : resources(std::move(gameResources))
        , window(resources, settings) {
        window.show();
        REQUIRE(QTest::qWaitForWindowExposed(&window));
    }

    /// The menu entry reading `text`. A dock's own toggle action carries the dock's title too, so
    /// "Selection" alone would also match the Selection dock's; only menu-owned actions count here.
    QAction* menuAction(const QString& text) const {
        return findChildWhere<QAction>(window, [&text](const QAction& action) {
            return plainText(action) == text && qobject_cast<const QMenu*>(action.parent()) != nullptr;
        });
    }

    /// Opens `map` in a new EditorWidget, as loading a map does; needs editorResources().
    EditorWidget& openMap(std::unique_ptr<Map> map) {
        window.setEditorWidget(std::make_unique<EditorWidget>(*resources, std::move(map)));
        QApplication::processEvents();
        auto* editor = window.findChild<EditorWidget*>();
        REQUIRE(editor != nullptr);
        return *editor;
    }
};

} // namespace geck::test
