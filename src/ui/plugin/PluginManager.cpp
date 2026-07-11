#include "ui/plugin/PluginManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <utility>

#include "Application.h"
#include "scripting/MapScriptApi.h"
#include "scripting/PluginVm.h"

namespace geck::plugin {

namespace {

    // The writable user plugins dir, created on first use — same <ConfigLocation>/gecko/<...>
    // convention as PatternLibrary::rootDir and Settings.
    QString userPluginsDir() {
        const QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        const QString dir = QDir(configPath).filePath(QStringLiteral("gecko/plugins"));
        QDir().mkpath(dir);
        return dir;
    }

    // The read-only plugins shipped alongside the editor's other resources.
    QString bundledPluginsDir() {
        return QString::fromStdString((Application::getResourcesPath() / "plugins").string());
    }

} // namespace

PluginManager::PluginManager() = default;
PluginManager::~PluginManager() = default;

PluginManager::Plugin* PluginManager::find(const QString& id) {
    for (auto& plugin : _plugins) {
        if (plugin.manifest.id == id) {
            return &plugin;
        }
    }
    return nullptr;
}

const PluginManager::Plugin* PluginManager::find(const QString& id) const {
    for (const auto& plugin : _plugins) {
        if (plugin.manifest.id == id) {
            return &plugin;
        }
    }
    return nullptr;
}

void PluginManager::scanRoot(const QString& root, const QString& source, std::vector<Plugin>& previous) {
    QDir dir(root);
    if (!dir.exists()) {
        return;
    }
    const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& sub : subdirs) {
        const QString pluginDir = dir.filePath(sub);
        const QString manifestPath = QDir(pluginDir).filePath(QStringLiteral("plugin.json"));
        if (!QFileInfo::exists(manifestPath)) {
            continue; // a directory without a manifest is not a plugin
        }

        Plugin plugin;
        plugin.dir = pluginDir;
        plugin.source = source;

        QFile file(manifestPath);
        if (!file.open(QIODevice::ReadOnly)) {
            // A manifest we cannot read is a Faulted row keyed by its directory name, not a drop.
            plugin.manifest.id = sub;
            plugin.manifest.name = sub;
            plugin.state = State::Faulted;
            plugin.error = QStringLiteral("could not open plugin.json");
        } else {
            QString error;
            if (const auto parsed = PluginManifest::parse(file.readAll(), error)) {
                plugin.manifest = *parsed;
            } else {
                plugin.manifest.id = sub;
                plugin.manifest.name = sub;
                plugin.state = State::Faulted;
                plugin.error = error;
            }
        }

        // Dedupe by id: an earlier root (user) shadows a later one (bundled).
        if (find(plugin.manifest.id) != nullptr) {
            continue;
        }

        // Carry a still-present Enabled VM across the rescan so re-scanning never kills a running
        // plugin (its api already points at the live editor and stays valid).
        for (auto& prev : previous) {
            if (prev.manifest.id == plugin.manifest.id && prev.state == State::Enabled && prev.vm) {
                plugin.state = prev.state;
                plugin.error = prev.error;
                plugin.api = std::move(prev.api);
                plugin.vm = std::move(prev.vm);
                break;
            }
        }

        _plugins.push_back(std::move(plugin));
    }
}

void PluginManager::discover() {
    std::vector<Plugin> previous = std::move(_plugins);
    _plugins.clear();
    scanRoot(userPluginsDir(), QStringLiteral("user"), previous);
    scanRoot(bundledPluginsDir(), QStringLiteral("bundled"), previous);
}

void PluginManager::discoverIn(const QStringList& roots) {
    std::vector<Plugin> previous = std::move(_plugins);
    _plugins.clear();
    for (const QString& root : roots) {
        scanRoot(root, QStringLiteral("user"), previous);
    }
}

void PluginManager::retargetOne(Plugin& plugin) const {
    if (!plugin.api) {
        return;
    }
    if (_bound && _resources != nullptr && _hexgrid != nullptr && _controller != nullptr) {
        // buildSprites=true: plugins run on the UI thread with a live GL context, like
        // EditorWidget::runScript, so placed objects (a later write phase) also get a sprite.
        plugin.api->retarget(*_resources, *_hexgrid, *_controller, _map, _elevation, true);
    } else {
        plugin.api->detach();
    }
}

void PluginManager::setEditorBinding(resource::GameResources& resources, const HexagonGrid& hexgrid,
    ObjectCommandController& controller, Map* map, int elevation) {
    _resources = &resources;
    _hexgrid = &hexgrid;
    _controller = &controller;
    _map = map;
    _elevation = elevation;
    _bound = true;
    for (auto& plugin : _plugins) {
        if (plugin.state == State::Enabled) {
            retargetOne(plugin);
        }
    }
}

void PluginManager::clearEditorBinding() {
    _resources = nullptr;
    _hexgrid = nullptr;
    _controller = nullptr;
    _map = nullptr;
    _elevation = 0;
    _bound = false;
    for (auto& plugin : _plugins) {
        if (plugin.api) {
            plugin.api->detach();
        }
    }
}

bool PluginManager::enable(const QString& id) {
    Plugin* plugin = find(id);
    if (plugin == nullptr) {
        return false;
    }

    // A row whose manifest never validated has no entry to run; keep its discovery-time parse
    // error instead of overwriting it with a misleading "could not read entry script". A validated
    // manifest always has a non-empty entry (PluginManifest::parse requires it).
    if (plugin->manifest.entry.isEmpty()) {
        return false;
    }

    // Read the entry script first so a missing/unreadable file fails before we spin up a VM.
    const QString entryPath = QDir(plugin->dir).filePath(plugin->manifest.entry);
    QFile entryFile(entryPath);
    if (!entryFile.open(QIODevice::ReadOnly)) {
        plugin->vm.reset();
        plugin->api.reset();
        plugin->state = State::Faulted;
        plugin->error = QStringLiteral("could not read entry script \"%1\"").arg(plugin->manifest.entry);
        return false;
    }
    const std::string source = entryFile.readAll().toStdString();

    // A fresh VM every enable: re-enabling must never resume a previous faulting state. Tear down
    // any prior VM *before* replacing the api it borrows, honouring the VM-outlived-by-api contract
    // (a re-enable of a still-faulted plugin would otherwise free the api under the old VM).
    plugin->vm.reset();
    plugin->api = std::make_unique<MapScriptApi>();
    PluginVm::Config config;
    config.name = plugin->manifest.id.toStdString();
    // MVP: read-only — config.allowMapWrite stays false, so the map-mutating api half is not bound.
    plugin->vm = std::make_unique<PluginVm>(config, *plugin->api);

    // Point at the current editor before the entry runs, so an on-load map query sees the open map.
    retargetOne(*plugin);

    if (!plugin->vm->start()) {
        plugin->state = State::Faulted;
        plugin->error = QStringLiteral("the plugin VM failed to start");
        return false;
    }
    if (!plugin->vm->dispatch(source)) {
        plugin->state = State::Faulted;
        plugin->error = QString::fromStdString(plugin->vm->lastError());
        return false;
    }

    plugin->state = State::Enabled;
    plugin->error.clear();
    return true;
}

bool PluginManager::disable(const QString& id) {
    Plugin* plugin = find(id);
    if (plugin == nullptr) {
        return false;
    }
    plugin->vm.reset();
    plugin->api.reset();
    plugin->state = State::Disabled;
    plugin->error.clear();
    return true;
}

std::vector<PluginManager::Info> PluginManager::list() const {
    std::vector<Info> out;
    out.reserve(_plugins.size());
    for (const auto& plugin : _plugins) {
        Info info;
        info.id = plugin.manifest.id;
        info.name = plugin.manifest.name.isEmpty() ? plugin.manifest.id : plugin.manifest.name;
        info.version = plugin.manifest.version;
        info.description = plugin.manifest.description;
        info.source = plugin.source;
        info.state = plugin.state;
        info.error = plugin.error;
        if (plugin.vm) {
            info.console = QString::fromStdString(plugin.vm->console());
        }
        out.push_back(std::move(info));
    }
    return out;
}

} // namespace geck::plugin
