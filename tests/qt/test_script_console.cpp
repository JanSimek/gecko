#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QObject>
#include <QString>

#include "support/QtFind.h"
#include "ui/panels/ScriptConsoleWidget.h"

namespace test = geck::test;

TEST_CASE("Script console setSource loads the editor and feeds Run", "[script_console][scripting]") {
    geck::ScriptConsoleWidget console;

    // setSource is what "Execute script" in the file browser uses to drop a .luau into the console.
    const QString src = "api:paintFloor(0, 271)";
    console.setSource(src);

    QString runSource;
    QObject::connect(&console, &geck::ScriptConsoleWidget::runRequested,
        [&runSource](const QString& s) { runSource = s; });

    // Pressing Run hands back exactly what setSource put in the editor.
    QAbstractButton* run = test::findButton(console, "Run");
    REQUIRE(run != nullptr);
    run->click();
    CHECK(runSource == src);
}
