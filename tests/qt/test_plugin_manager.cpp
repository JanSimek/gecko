#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <optional>

#include "ui/plugin/PluginManager.h"

#include "support/ControllerFixture.h"

using geck::plugin::PluginManager;
using geck::test::ControllerFixture;
using State = geck::plugin::PluginManager::State;

namespace {

constexpr int ELEV = 0;

QString manifestJson(const QString& id, const QString& name, const QString& entry) {
    return QStringLiteral(R"({ "id": "%1", "name": "%2", "version": "1.0", "entry": "%3" })")
        .arg(id, name, entry);
}

// Materialize a plugin directory <root>/<id>/ with a plugin.json and (optionally) an entry file.
void writePlugin(const QString& root, const QString& id, const QString& manifest,
    const QString& entryName = QString(), const QString& entrySource = QString()) {
    const QString dir = root + QLatin1Char('/') + id;
    QDir().mkpath(dir);
    {
        QFile file(dir + QStringLiteral("/plugin.json"));
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(manifest.toUtf8());
    }
    if (!entryName.isEmpty()) {
        QFile file(dir + QLatin1Char('/') + entryName);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(entrySource.toUtf8());
    }
}

std::optional<PluginManager::Info> infoFor(const PluginManager& manager, const QString& id) {
    for (const auto& info : manager.list()) {
        if (info.id == id) {
            return info;
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("PluginManager discovers manifests and faults invalid ones", "[qt][plugins][manager]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    writePlugin(tmp.path(), "good", manifestJson("good", "Good", "g.luau"), "g.luau", "print('hi')");
    writePlugin(tmp.path(), "broken", QStringLiteral("{ not json"));
    // A directory with no plugin.json is simply not a plugin.
    QDir().mkpath(tmp.path() + QStringLiteral("/notaplugin"));

    PluginManager manager;
    manager.discoverIn({ tmp.path() });

    CHECK(manager.count() == 2);
    const auto good = infoFor(manager, "good");
    REQUIRE(good.has_value());
    CHECK(good->state == State::Discovered);
    const auto broken = infoFor(manager, "broken");
    REQUIRE(broken.has_value());
    CHECK(broken->state == State::Faulted);
    CHECK_FALSE(broken->error.isEmpty());
}

TEST_CASE("PluginManager runs an enabled plugin read-only against the current map", "[qt][plugins][manager]") {
    ControllerFixture fx;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // Read the map, and prove the mutating half of the api is simply absent.
    writePlugin(tmp.path(), "reader", manifestJson("reader", "Reader", "r.luau"), "r.luau",
        "print('floor=' .. api:getFloor(0)) "
        "print(api.paintFloor == nil and 'write-absent' or 'write-present')");

    PluginManager manager;
    manager.discoverIn({ tmp.path() });
    manager.setEditorBinding(fx.resources, fx.hexgrid, fx.controller, fx.map.get(), ELEV);

    REQUIRE(manager.enable("reader"));
    const auto info = infoFor(manager, "reader");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Enabled);
    CHECK(info->console.contains("floor="));
    CHECK(info->console.contains("write-absent"));
}

TEST_CASE("PluginManager is read-only: a write attempt faults and leaves the map untouched", "[qt][plugins][manager]") {
    ControllerFixture fx;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    writePlugin(tmp.path(), "writer", manifestJson("writer", "Writer", "w.luau"), "w.luau",
        "api:paintFloor(0, 271)");

    PluginManager manager;
    manager.discoverIn({ tmp.path() });
    manager.setEditorBinding(fx.resources, fx.hexgrid, fx.controller, fx.map.get(), ELEV);

    const auto before = fx.mapFile().tiles.at(ELEV)[0].getFloor();
    // paintFloor is not bound in a read-only VM, so the call is an ordinary fault.
    CHECK_FALSE(manager.enable("writer"));
    const auto info = infoFor(manager, "writer");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Faulted);
    CHECK(fx.mapFile().tiles.at(ELEV)[0].getFloor() == before);
}

TEST_CASE("PluginManager marks a plugin Faulted when its entry script errors", "[qt][plugins][manager]") {
    ControllerFixture fx;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    writePlugin(tmp.path(), "bad", manifestJson("bad", "Bad", "b.luau"), "b.luau", "error('boom')");

    PluginManager manager;
    manager.discoverIn({ tmp.path() });
    manager.setEditorBinding(fx.resources, fx.hexgrid, fx.controller, fx.map.get(), ELEV);

    CHECK_FALSE(manager.enable("bad"));
    const auto info = infoFor(manager, "bad");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Faulted);
    CHECK(info->error.contains("boom"));
}

TEST_CASE("PluginManager faults an enable when the entry file is missing", "[qt][plugins][manager]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // Manifest names an entry that does not exist on disk.
    writePlugin(tmp.path(), "noentry", manifestJson("noentry", "No Entry", "missing.luau"));

    PluginManager manager;
    manager.discoverIn({ tmp.path() });

    CHECK_FALSE(manager.enable("noentry"));
    const auto info = infoFor(manager, "noentry");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Faulted);
    CHECK(info->error.contains("entry"));
}

TEST_CASE("PluginManager detaches the api on unbind and rebinds on re-enable", "[qt][plugins][manager]") {
    ControllerFixture fx;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // A map read guarded by pcall so 'no map open' is reported, not a fault.
    writePlugin(tmp.path(), "probe", manifestJson("probe", "Probe", "p.luau"), "p.luau",
        "local ok = pcall(function() return api:getFloor(0) end) "
        "print(ok and 'read-ok' or 'no-map')");

    PluginManager manager;
    manager.discoverIn({ tmp.path() });

    // Bound: the map read succeeds.
    manager.setEditorBinding(fx.resources, fx.hexgrid, fx.controller, fx.map.get(), ELEV);
    REQUIRE(manager.enable("probe"));
    CHECK(infoFor(manager, "probe")->console.contains("read-ok"));

    // Unbound: a fresh enable builds a detached api, so the map read reports no map.
    manager.clearEditorBinding();
    REQUIRE(manager.enable("probe"));
    CHECK(infoFor(manager, "probe")->console.contains("no-map"));
}

TEST_CASE("PluginManager keeps an enabled plugin running across a rescan", "[qt][plugins][manager]") {
    ControllerFixture fx;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    writePlugin(tmp.path(), "reader", manifestJson("reader", "Reader", "r.luau"), "r.luau",
        "print('enabled-once')");

    PluginManager manager;
    manager.discoverIn({ tmp.path() });
    manager.setEditorBinding(fx.resources, fx.hexgrid, fx.controller, fx.map.get(), ELEV);
    REQUIRE(manager.enable("reader"));

    manager.discoverIn({ tmp.path() }); // rescan
    const auto info = infoFor(manager, "reader");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Enabled);          // still enabled
    CHECK(info->console.contains("enabled-once")); // same VM, its console carried over
}

TEST_CASE("PluginManager dedupes by id with the earlier root shadowing", "[qt][plugins][manager]") {
    QTemporaryDir userRoot;
    QTemporaryDir bundledRoot;
    REQUIRE(userRoot.isValid());
    REQUIRE(bundledRoot.isValid());
    writePlugin(userRoot.path(), "dup", manifestJson("dup", "User Copy", "e.luau"), "e.luau", "");
    writePlugin(bundledRoot.path(), "dup", manifestJson("dup", "Bundled Copy", "e.luau"), "e.luau", "");

    PluginManager manager;
    manager.discoverIn({ userRoot.path(), bundledRoot.path() });

    CHECK(manager.count() == 1);
    const auto info = infoFor(manager, "dup");
    REQUIRE(info.has_value());
    CHECK(info->name == QStringLiteral("User Copy")); // the earlier root won
}

TEST_CASE("PluginManager disable tears the VM down and clears the console", "[qt][plugins][manager]") {
    ControllerFixture fx;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    writePlugin(tmp.path(), "reader", manifestJson("reader", "Reader", "r.luau"), "r.luau",
        "print('hi')");

    PluginManager manager;
    manager.discoverIn({ tmp.path() });
    manager.setEditorBinding(fx.resources, fx.hexgrid, fx.controller, fx.map.get(), ELEV);
    REQUIRE(manager.enable("reader"));
    REQUIRE(infoFor(manager, "reader")->console.contains("hi"));

    CHECK(manager.disable("reader"));
    const auto info = infoFor(manager, "reader");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Disabled);
    CHECK(info->console.isEmpty());

    CHECK_FALSE(manager.disable("nonexistent"));
}
