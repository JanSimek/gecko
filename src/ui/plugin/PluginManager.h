#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include "ui/plugin/PluginManifest.h"

namespace geck {
class HexagonGrid;
class Map;
class MapScriptApi;
class ObjectCommandController;
class PluginVm;
namespace resource {
    class GameResources;
}
} // namespace geck

namespace geck::plugin {

/// Discovers, owns and runs the editor's Luau plugins.
///
/// MVP scope: each enabled plugin is a resident, read-only `PluginVm` that runs its entry script
/// once when enabled (tools, menus, event handlers and the map-write permission model land in
/// later phases). The manager keeps every enabled plugin's `MapScriptApi` pointed at the current
/// editor — retarget() on map open, detach() on close — so a plugin survives map switches without
/// restarting and never dereferences a torn-down editor. It never stores a long-lived
/// `GameResources&`: that object can be rebuilt on a data-path change, so the binding is passed
/// fresh each map open (and cleared before the old resources die).
class PluginManager {
public:
    enum class State {
        Discovered, ///< Manifest parsed; VM not built yet.
        Enabled,    ///< VM built and entry script ran cleanly.
        Disabled,   ///< Explicitly turned off; no VM.
        Faulted,    ///< Manifest invalid, or the VM failed to start / the entry script errored.
    };

    /// A flat snapshot for the manager UI (no VM pointers cross this boundary).
    struct Info {
        QString id;
        QString name;
        QString version;
        QString description;
        QString source; ///< "user" or "bundled".
        State state = State::Discovered;
        QString error;   ///< Faulted reason (parse or enable failure), else empty.
        QString console; ///< The VM's bounded print/fault console when enabled, else empty.
    };

    PluginManager();
    ~PluginManager();
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    /// Scan the user plugins dir (writable, `<ConfigLocation>/gecko/plugins`) and the bundled dir
    /// (`<resources>/plugins`, read-only), parse every `<dir>/plugin.json`, and dedupe by id with
    /// the user copy shadowing the bundled one. Invalid manifests become Faulted rows keyed by
    /// their directory name, never silently dropped. A rescan preserves plugins that are still
    /// present and Enabled (their running VM is carried over untouched).
    void discover();
    /// Discovery against explicit roots (earlier root shadows later on id) — the injectable seam
    /// the headless tests drive, and the mechanism `discover()` builds on.
    void discoverIn(const QStringList& roots);

    /// Point every enabled plugin at the current editor and map (map may be null: host alive, no
    /// map open). Called on map open / switch, and remembered so a plugin enabled afterwards binds
    /// to the same editor immediately.
    void setEditorBinding(resource::GameResources& resources, const HexagonGrid& hexgrid,
        ObjectCommandController& controller, Map* map, int elevation);
    /// The editor is going away (map closed, resources about to be rebuilt, or app teardown):
    /// detach every enabled plugin's api and forget the binding. Plugins stay enabled but their
    /// map/host calls raise until the next setEditorBinding().
    void clearEditorBinding();

    /// Build the plugin's read-only VM, start it, and run its entry script once. Binds to the
    /// current editor if one is set. On any failure (unreadable entry, VM start failure, a script
    /// error) the plugin becomes Faulted with the reason on its console; returns true only on a
    /// clean run.
    bool enable(const QString& id);
    /// Tear down the plugin's VM and mark it Disabled. Returns false if the id is unknown.
    bool disable(const QString& id);

    /// Snapshot of every discovered plugin, in discovery order, for the manager UI.
    [[nodiscard]] std::vector<Info> list() const;
    /// Number of discovered plugins (including Faulted rows).
    [[nodiscard]] std::size_t count() const { return _plugins.size(); }

private:
    struct Plugin {
        PluginManifest manifest;
        QString dir;    ///< Absolute plugin directory.
        QString source; ///< "user" or "bundled".
        State state = State::Discovered;
        QString error;
        // Owned so the VM's borrowed api (a reference) always outlives the VM. Present only while
        // Enabled; destroyed on disable().
        std::unique_ptr<MapScriptApi> api;
        std::unique_ptr<PluginVm> vm;
    };

    // Scan one root for `<root>/*/plugin.json`, appending Plugin rows. Skips ids already present
    // (earlier roots win). Carries over a still-present Enabled VM from `previous` by id.
    void scanRoot(const QString& root, const QString& source, std::vector<Plugin>& previous);
    Plugin* find(const QString& id);
    [[nodiscard]] const Plugin* find(const QString& id) const;
    // Point one plugin's api at the current binding (or detach it when unbound).
    void retargetOne(Plugin& plugin) const;

    std::vector<Plugin> _plugins;

    // Current editor binding, refreshed by setEditorBinding / nulled by clearEditorBinding. Raw and
    // never owning: see the class comment on the resources-rebuild ordering that keeps this safe.
    resource::GameResources* _resources = nullptr;
    const HexagonGrid* _hexgrid = nullptr;
    ObjectCommandController* _controller = nullptr;
    Map* _map = nullptr;
    int _elevation = 0;
    bool _bound = false;
};

} // namespace geck::plugin
