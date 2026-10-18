#include "cli/MapExamples.h"

#include "cli/MapAnalyzer.h" // listMapPaths
#include "cli/MapLoad.h"     // loadMap
#include "cli/ProtoName.h"
#include "cli/ScriptRefs.h" // scriptsCalling
#include "editor/HexGeometry.h"
#include "format/ai/AiPacket.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "reader/TextParsing.h"
#include "resource/AiTxtLoader.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptNames.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;
    using text::toLower;

    constexpr std::size_t MAX_NEIGHBOUR_GROUPS = 30;

    const char* kindOf(const MapObject& object) {
        if (object.isExitGridMarker()) {
            return "exitgrid";
        }
        using enum ObjectType;
        switch (ProtoId(object.pro_pid).objectType()) {
            case Item:
                return "item";
            case Critter:
                return "critter";
            case Scenery:
                return "scenery";
            case Wall:
                return "wall";
            case Misc:
                return "misc";
            default:
                return "unknown";
        }
    }

    bool contains(const std::string& haystack, const std::string& lowerNeedle) {
        return toLower(haystack).find(lowerNeedle) != std::string::npos;
    }

    // "klamall", "KLAMALL.MAP" and "maps/klamall.map" all name the same map.
    std::string mapKey(const std::string& map) {
        std::string name = toLower(std::filesystem::path(map).filename().string());
        if (!name.ends_with(".map")) {
            name += ".map";
        }
        return name;
    }

    // One object of a map: on the floor, or carried (with the hex of whoever carries it).
    struct Placed {
        const MapObject* object = nullptr;
        int elevation = 0;
        int hex = 0;
        const MapObject* holder = nullptr;
    };

    // The lookups every map shares: proto names, script names and the query's lowercased strings.
    class Context {
    public:
        Context(resource::GameResources& resources, const ExamplesOptions& options)
            : _resources(resources)
            , _options(options)
            , _ai(resource::loadAiTxt(resources))
            , _name(toLower(options.name))
            , _script(toLower(options.script))
            , _carries(toLower(options.carries))
            , _near(toLower(options.near)) {
            try {
                _scriptsLst = resources.repository().tryLoad<Lst>(ResourcePaths::Lst::SCRIPTS);
            } catch (const std::exception& e) {
                spdlog::debug("examples: no scripts.lst: {}", e.what());
            }
            for (const auto& map : options.maps) {
                _maps.insert(mapKey(map));
            }
        }

        const std::string& protoName(uint32_t pid) {
            if (const auto it = _protoNames.find(pid); it != _protoNames.end()) {
                return it->second;
            }
            return _protoNames.try_emplace(pid, protoDisplayName(_resources, pid)).first->second;
        }

        // The scripts.lst entry without its extension, e.g. "dcmerch".
        std::string scriptName(int programIndex) const {
            if (_scriptsLst == nullptr || programIndex < 0 || programIndex >= static_cast<int>(_scriptsLst->list().size())) {
                return {};
            }
            return toLower(std::filesystem::path(_scriptsLst->at(programIndex)).stem().string());
        }

        // The script filter: a substring of the scripts.lst name or of its description, any case.
        bool scriptMatches(int programIndex) {
            auto [it, inserted] = _scriptMatches.try_emplace(programIndex, false);
            if (inserted) {
                it->second = contains(scriptName(programIndex), _script)
                    || contains(resource::scriptDescription(_resources, programIndex), _script);
            }
            return it->second;
        }

        resource::GameResources& resources() { return _resources; }
        const ExamplesOptions& options() const { return _options; }
        const AiTxt& ai() const { return _ai; }
        const std::string& name() const { return _name; }
        const std::string& script() const { return _script; }
        const std::string& carries() const { return _carries; }
        const std::string& near() const { return _near; }
        bool wantsMap(const std::string& mapPath) const { return _maps.empty() || _maps.contains(mapKey(mapPath)); }

        std::optional<std::set<std::string, std::less<>>> calling; ///< scripts the scriptCalls filter accepts

    private:
        resource::GameResources& _resources;
        const ExamplesOptions& _options;
        const Lst* _scriptsLst = nullptr;
        AiTxt _ai;
        std::string _name;
        std::string _script;
        std::string _carries;
        std::string _near;
        std::set<std::string, std::less<>> _maps;
        std::unordered_map<uint32_t, std::string> _protoNames;
        std::unordered_map<int, bool> _scriptMatches;
    };

    // One loaded map, indexed for the query: every object (inventories included), the program index
    // behind each script SID, and the floor objects by elevation and hex for the neighbour search.
    class MapView {
    public:
        explicit MapView(const Map& map) {
            for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
                for (const MapScript& script : map.getMapFile().map_scripts[section]) {
                    _programs.try_emplace(script.pid, static_cast<int>(script.script_id));
                }
            }
            for (const auto& [elevation, objects] : map.getMapFile().map_objects) {
                for (const auto& object : objects) {
                    if (object) {
                        add(*object, elevation, static_cast<int>(object->position), nullptr);
                    }
                }
            }
        }

        const std::vector<Placed>& placed() const { return _placed; }

        int programIndex(const MapObject& object) const {
            if (object.map_scripts_pid == -1) {
                return -1;
            }
            const auto it = _programs.find(static_cast<uint32_t>(object.map_scripts_pid));
            return it == _programs.end() ? -1 : it->second;
        }

        // The floor objects within `radius` of `hex`, nearest first, as (object, distance).
        std::vector<std::pair<const MapObject*, int>> around(int elevation, int hex, int radius) const {
            std::vector<std::pair<const MapObject*, int>> out;
            const auto floor = _floors.find(elevation);
            if (floor == _floors.end()) {
                return out;
            }
            for (const auto& [position, distance] : hexgrid::hexDiscByDistance(hex, radius)) {
                const auto [first, last] = floor->second.equal_range(position);
                for (auto it = first; it != last; ++it) {
                    out.emplace_back(it->second, distance);
                }
            }
            std::ranges::stable_sort(out, {}, &std::pair<const MapObject*, int>::second);
            return out;
        }

    private:
        void add(const MapObject& object, int elevation, int hex, const MapObject* holder) {
            _placed.push_back({ &object, elevation, hex, holder });
            if (holder == nullptr) {
                _floors[elevation].emplace(hex, &object);
            }
            for (const auto& carried : object.inventory) {
                if (carried) {
                    add(*carried, elevation, hex, &object);
                }
            }
        }

        std::vector<Placed> _placed;
        std::unordered_map<uint32_t, int> _programs;
        std::map<int, std::unordered_multimap<int, const MapObject*>> _floors;
    };

    bool carriesMatch(Context& context, const MapObject& object) {
        return std::ranges::any_of(object.inventory, [&context](const auto& carried) {
            return carried && contains(context.protoName(carried->pro_pid), context.carries());
        });
    }

    bool nearMatch(Context& context, const MapView& view, const Placed& placed) {
        const int radius = context.options().nearRadius;
        return std::ranges::any_of(view.around(placed.elevation, placed.hex, radius), [&context, &placed](const auto& neighbour) {
            const MapObject* object = neighbour.first;
            return object != placed.object && object != placed.holder && contains(context.protoName(object->pro_pid), context.near());
        });
    }

    bool matches(Context& context, const MapView& view, const Placed& placed) {
        const ExamplesOptions& options = context.options();
        const MapObject& object = *placed.object;
        if (!options.kind.empty() && options.kind != kindOf(object)) {
            return false;
        }
        if (!options.pids.empty() && std::ranges::find(options.pids, object.pro_pid) == options.pids.end()) {
            return false;
        }
        if (!context.name().empty() && !contains(context.protoName(object.pro_pid), context.name())) {
            return false;
        }
        if (!context.script().empty() || context.calling.has_value()) {
            const int programIndex = view.programIndex(object);
            const std::string script = context.scriptName(programIndex);
            if (script.empty() || (!context.script().empty() && !context.scriptMatches(programIndex))) {
                return false;
            }
            if (context.calling.has_value() && !context.calling->contains(script)) {
                return false;
            }
        }
        if (!context.carries().empty() && !carriesMatch(context, object)) {
            return false;
        }
        return context.near().empty() || nearMatch(context, view, placed);
    }

    ordered_json scriptJson(Context& context, int programIndex) {
        if (programIndex < 0) {
            return nullptr;
        }
        return { { "programIndex", programIndex }, { "name", context.scriptName(programIndex) },
            { "description", resource::scriptDescription(context.resources(), programIndex) } };
    }

    ordered_json objectRef(Context& context, const MapObject& object) {
        return { { "kind", kindOf(object) }, { "pid", object.pro_pid }, { "name", context.protoName(object.pro_pid) } };
    }

    // What stands around a match, one row per proto: how many and the nearest distance, with the
    // scripts attached to them (a door's lock script, a shelf's stock script).
    ordered_json neighboursJson(Context& context, const MapView& view, const Placed& placed) {
        struct Group {
            const MapObject* first = nullptr;
            int count = 0;
            int nearest = 0;
            std::set<std::string, std::less<>> scripts;
        };
        std::vector<Group> groups;
        std::unordered_map<uint32_t, std::size_t> byPid;
        for (const auto& [object, distance] : view.around(placed.elevation, placed.hex, context.options().contextRadius)) {
            if (object == placed.object || object == placed.holder) {
                continue;
            }
            auto [it, inserted] = byPid.try_emplace(object->pro_pid, groups.size());
            if (inserted) {
                groups.push_back({ object, 0, distance, {} });
            }
            Group& group = groups[it->second];
            ++group.count;
            if (const std::string script = context.scriptName(view.programIndex(*object)); !script.empty()) {
                group.scripts.insert(script);
            }
        }
        auto out = ordered_json::array();
        for (const Group& group : groups) {
            if (out.size() == MAX_NEIGHBOUR_GROUPS) {
                break;
            }
            ordered_json row = objectRef(context, *group.first);
            row["count"] = group.count;
            row["nearest"] = group.nearest;
            if (!group.scripts.empty()) {
                row["scripts"] = group.scripts;
            }
            out.push_back(std::move(row));
        }
        return out;
    }

    ordered_json matchJson(Context& context, const MapView& view, const Placed& placed, const ordered_json& mapRef) {
        const MapObject& object = *placed.object;
        ordered_json match = mapRef;
        match["elevation"] = placed.elevation;
        match["hex"] = placed.hex;
        match["col"] = hexgrid::columnOf(placed.hex);
        match["row"] = hexgrid::rowOf(placed.hex);
        match.update(objectRef(context, object));
        if (object.amount > 1) {
            match["qty"] = object.amount;
        }
        if (const ordered_json script = scriptJson(context, view.programIndex(object)); !script.is_null()) {
            match["script"] = script;
        }
        if (placed.holder != nullptr) {
            match["holder"] = objectRef(context, *placed.holder);
        }
        if (ProtoId(object.pro_pid).objectType() == ObjectType::Critter) {
            const AiPacket* packet = context.ai().byPacketNum(static_cast<int>(object.ai_packet));
            match["critter"] = { { "team", object.group_id }, { "aiPacket", object.ai_packet },
                { "aiPacketName", packet != nullptr ? ordered_json(packet->name) : ordered_json(nullptr) } };
        }
        if (!object.inventory.empty()) {
            auto inventory = ordered_json::array();
            for (const auto& carried : object.inventory) {
                if (carried) {
                    ordered_json item = objectRef(context, *carried);
                    item["qty"] = carried->amount;
                    inventory.push_back(std::move(item));
                }
            }
            match["inventory"] = std::move(inventory);
        }
        match["neighbours"] = neighboursJson(context, view, placed);
        return match;
    }

    bool hasQuery(const ExamplesOptions& options) {
        return !options.kind.empty() || !options.pids.empty() || !options.name.empty() || !options.script.empty()
            || !options.scriptCalls.empty() || !options.carries.empty();
    }

} // namespace

int findExamples(resource::GameResources& resources, const ExamplesOptions& options, std::ostream& out) {
    if (!hasQuery(options)) {
        out << "examples: give at least one of kind, pid, name, script, scriptCalls or carries\n";
        return 1;
    }
    const std::vector<std::string> mapPaths = listMapPaths(resources.files());
    if (mapPaths.empty()) {
        out << "examples: no maps found (is the Fallout 2 data mounted?)\n";
        return 1;
    }

    Context context(resources, options);
    if (!options.scriptCalls.empty()) {
        std::size_t scriptsRead = 0;
        context.calling = scriptsCalling(resources, options.scriptCalls, &scriptsRead);
        if (scriptsRead == 0) {
            out << "examples: scriptCalls needs a script-source tree (e.g. FRP scripts_src) mounted\n";
            return 1;
        }
    }

    const resource::MapNameResolver names(resources);
    auto found = ordered_json::array();
    std::vector<ordered_json> byMap;
    auto unreadable = ordered_json::array();
    int total = 0;
    for (const std::string& mapPath : mapPaths) {
        if (!context.wantsMap(mapPath)) {
            continue;
        }
        std::string loadError;
        const std::unique_ptr<Map> map = loadMap(resources, mapPath, &loadError);
        if (map == nullptr) {
            unreadable.push_back({ { "map", mapPath }, { "reason", loadError } });
            continue;
        }
        const std::string file = std::filesystem::path(mapPath).filename().string();
        const std::string display = names.displayName(names.indexOf(file), 0);
        const ordered_json mapRef = { { "map", file }, { "displayName", display.empty() ? ordered_json(nullptr) : ordered_json(display) } };

        const MapView view(*map);
        int onMap = 0;
        for (const Placed& placed : view.placed()) {
            if (!matches(context, view, placed)) {
                continue;
            }
            ++onMap;
            if (static_cast<int>(found.size()) < options.limit) {
                found.push_back(matchJson(context, view, placed, mapRef));
            }
        }
        if (onMap > 0) {
            ordered_json row = mapRef;
            row["count"] = onMap;
            byMap.push_back(std::move(row));
        }
        total += onMap;
    }
    std::ranges::stable_sort(byMap, std::greater<>{}, [](const ordered_json& row) { return row["count"].get<int>(); });

    ordered_json root;
    root["total"] = total;
    root["returned"] = found.size();
    root["truncated"] = total > static_cast<int>(found.size());
    root["byMap"] = std::move(byMap);
    root["matches"] = std::move(found);
    root["mapsUnreadable"] = std::move(unreadable);
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
