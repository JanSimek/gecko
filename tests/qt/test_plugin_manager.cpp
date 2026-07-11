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

// A manager over a private temp plugin root plus a headless editing context, so each test states
// only what differs (which plugins exist, whether the editor is bound) rather than the scaffolding.
struct ManagerFixture {
    ControllerFixture fx;
    QTemporaryDir tmp;
    PluginManager manager;

    ManagerFixture() { REQUIRE(tmp.isValid()); }

    void add(const QString& id, const QString& name, const QString& entryName,
        const QString& entrySource = QString()) {
        writePlugin(tmp.path(), id, manifestJson(id, name, entryName), entryName, entrySource);
    }
    void addRawManifest(const QString& id, const QString& manifest) {
        writePlugin(tmp.path(), id, manifest);
    }
    void discover() { manager.discoverIn({ tmp.path() }); }
    void bind() {
        manager.setEditorBinding(fx.resources, fx.hexgrid, fx.controller, fx.map.get(), ELEV);
    }
    std::optional<PluginManager::Info> info(const QString& id) const { return infoFor(manager, id); }
    uint16_t floorAt(int tile) { return fx.mapFile().tiles.at(ELEV)[tile].getFloor(); }
};

} // namespace

TEST_CASE("PluginManager discovers manifests and faults invalid ones", "[qt][plugins][manager]") {
    ManagerFixture f;
    f.add("good", "Good", "g.luau", "print('hi')");
    f.addRawManifest("broken", QStringLiteral("{ not json"));
    // A directory with no plugin.json is simply not a plugin.
    QDir().mkpath(f.tmp.path() + QStringLiteral("/notaplugin"));
    f.discover();

    CHECK(f.manager.count() == 2);
    const auto good = f.info("good");
    REQUIRE(good.has_value());
    CHECK(good->state == State::Discovered);
    const auto broken = f.info("broken");
    REQUIRE(broken.has_value());
    CHECK(broken->state == State::Faulted);
    CHECK_FALSE(broken->error.isEmpty());
}

TEST_CASE("PluginManager runs an enabled plugin read-only against the current map", "[qt][plugins][manager]") {
    ManagerFixture f;
    // Read the map, and prove the mutating half of the api is simply absent.
    f.add("reader", "Reader", "r.luau",
        "print('floor=' .. api:getFloor(0)) "
        "print(api.paintFloor == nil and 'write-absent' or 'write-present')");
    f.discover();
    f.bind();

    REQUIRE(f.manager.enable("reader"));
    const auto info = f.info("reader");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Enabled);
    CHECK(info->console.contains("floor="));
    CHECK(info->console.contains("write-absent"));
}

TEST_CASE("PluginManager is read-only: a write attempt faults and leaves the map untouched", "[qt][plugins][manager]") {
    ManagerFixture f;
    f.add("writer", "Writer", "w.luau", "api:paintFloor(0, 271)");
    f.discover();
    f.bind();

    const auto before = f.floorAt(0);
    // paintFloor is not bound in a read-only VM, so the call is an ordinary fault.
    CHECK_FALSE(f.manager.enable("writer"));
    const auto info = f.info("writer");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Faulted);
    CHECK(f.floorAt(0) == before);
}

TEST_CASE("PluginManager marks a plugin Faulted when its entry script errors", "[qt][plugins][manager]") {
    ManagerFixture f;
    f.add("bad", "Bad", "b.luau", "error('boom')");
    f.discover();
    f.bind();

    CHECK_FALSE(f.manager.enable("bad"));
    const auto info = f.info("bad");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Faulted);
    CHECK(info->error.contains("boom"));
}

TEST_CASE("PluginManager faults an enable when the entry file is missing", "[qt][plugins][manager]") {
    ManagerFixture f;
    // Manifest names an entry, but the file is never written (addRawManifest writes only the
    // manifest — add() would create the entry file).
    f.addRawManifest("noentry", manifestJson("noentry", "No Entry", "missing.luau"));
    f.discover();

    CHECK_FALSE(f.manager.enable("noentry"));
    const auto info = f.info("noentry");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Faulted);
    CHECK(info->error.contains("entry"));
}

TEST_CASE("PluginManager keeps a bad manifest's parse error when enable is attempted", "[qt][plugins][manager]") {
    ManagerFixture f;
    f.addRawManifest("broken", QStringLiteral("{ not json"));
    f.discover();
    const QString parseError = f.info("broken")->error;
    REQUIRE_FALSE(parseError.isEmpty());

    // Enabling a row whose manifest never validated is a no-op that preserves the original error,
    // not a clobbering "could not read entry script" run.
    CHECK_FALSE(f.manager.enable("broken"));
    const auto info = f.info("broken");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Faulted);
    CHECK(info->error == parseError);
}

TEST_CASE("PluginManager detaches the api on unbind and rebinds on re-enable", "[qt][plugins][manager]") {
    ManagerFixture f;
    // A map read guarded by pcall so 'no map open' is reported, not a fault.
    f.add("probe", "Probe", "p.luau",
        "local ok = pcall(function() return api:getFloor(0) end) "
        "print(ok and 'read-ok' or 'no-map')");
    f.discover();

    // Bound: the map read succeeds.
    f.bind();
    REQUIRE(f.manager.enable("probe"));
    CHECK(f.info("probe")->console.contains("read-ok"));

    // Unbound: a fresh enable builds a detached api, so the map read reports no map.
    f.manager.clearEditorBinding();
    REQUIRE(f.manager.enable("probe"));
    CHECK(f.info("probe")->console.contains("no-map"));
}

TEST_CASE("PluginManager keeps an enabled plugin running across a rescan", "[qt][plugins][manager]") {
    ManagerFixture f;
    f.add("reader", "Reader", "r.luau", "print('enabled-once')");
    f.discover();
    f.bind();
    REQUIRE(f.manager.enable("reader"));

    f.discover(); // rescan
    const auto info = f.info("reader");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Enabled);          // still enabled
    CHECK(info->console.contains("enabled-once")); // same VM, its console carried over
}

TEST_CASE("PluginManager disable tears the VM down and clears the console", "[qt][plugins][manager]") {
    ManagerFixture f;
    f.add("reader", "Reader", "r.luau", "print('hi')");
    f.discover();
    f.bind();
    REQUIRE(f.manager.enable("reader"));
    REQUIRE(f.info("reader")->console.contains("hi"));

    CHECK(f.manager.disable("reader"));
    const auto info = f.info("reader");
    REQUIRE(info.has_value());
    CHECK(info->state == State::Disabled);
    CHECK(info->console.isEmpty());

    CHECK_FALSE(f.manager.disable("nonexistent"));
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
