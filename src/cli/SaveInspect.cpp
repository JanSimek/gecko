#include "cli/SaveInspect.h"

#include "cli/CritterCombatFlags.h"
#include "cli/GlobalVars.h"
#include "cli/MapLoad.h"
#include "format/gam/Gam.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "reader/IniParser.h"
#include "reader/ReaderExceptions.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;
    using GlobalDefaults = std::vector<std::pair<std::string, int>>;

    // Engine layout constants (fallout2-ce). They are code, not data, so they are cited rather than loaded.
    // loadsave.cc compares strncmp(signature, LOAD_SAVE_SIGNATURE, 18): the 17 characters and the NUL after them.
    constexpr const char* LOAD_SAVE_SIGNATURE = "FALLOUT SAVE FILE";
    constexpr std::size_t LS_PREVIEW_SIZE = 224 * 133;  // loadsave.cc LS_PREVIEW_WIDTH * HEIGHT
    constexpr std::size_t SAVEABLE_STAT_COUNT = 35;     // stat_defs.h
    constexpr std::size_t SKILL_COUNT = 18;             // skill_defs.h
    constexpr std::size_t KILL_TYPE_DEFAULT_COUNT = 19; // proto_types.h
    constexpr std::size_t NUM_TAGGED_SKILLS = 4;        // skill_defs.h
    constexpr std::size_t PERK_COUNT = 119;             // perk_defs.h
    constexpr uint32_t DUDE_PID = 0x01000000;           // critter.cc gDudeProto
    constexpr uint32_t CRITTER_DUDE_SNEAKING = 0x01;    // obj_types.h CritterFlags
    constexpr uint32_t COMBAT_STATE_IN_COMBAT = 0x01;   // combat_defs.h
    constexpr uint32_t COMBAT_STATE_PLAYER_TURN = 0x02;
    constexpr uint32_t COMBAT_STATE_EXIT_REQUESTED = 0x08;
    constexpr uint32_t COMBAT_STATE_KNOWN = COMBAT_STATE_IN_COMBAT | COMBAT_STATE_PLAYER_TURN | COMBAT_STATE_EXIT_REQUESTED;
    // Plausibility bounds for lengths read from the file: the slot's map list, and a combat list, which holds
    // the critters of one elevation and so cannot exceed its hex count (map_defs.h HEX_GRID_SIZE).
    constexpr int32_t MAX_MAP_LIST = 100000;
    constexpr int32_t MAX_COMBAT_LIST = 200 * 200;
    constexpr int ELEVATION_COUNT = 3; // map_defs.h

    class SaveReader {
    public:
        explicit SaveReader(const std::vector<uint8_t>& data)
            : _data(data) {
        }

        void require(std::size_t length, std::string_view what) const {
            if (_pos > _data.size() || length > _data.size() - _pos) {
                throw ParseException(std::format("SAVE.DAT ends inside {} (offset {}, need {} bytes, file is {})", what, _pos,
                    length, _data.size()));
            }
        }

        int32_t i32(std::string_view what) {
            require(4, what);
            const auto v = static_cast<int32_t>((uint32_t{ _data[_pos] } << 24) | (uint32_t{ _data[_pos + 1] } << 16)
                | (uint32_t{ _data[_pos + 2] } << 8) | uint32_t{ _data[_pos + 3] });
            _pos += 4;
            return v;
        }

        int16_t i16(std::string_view what) {
            require(2, what);
            const auto v = static_cast<int16_t>((_data[_pos] << 8) | _data[_pos + 1]);
            _pos += 2;
            return v;
        }

        uint8_t u8(std::string_view what) {
            require(1, what);
            return _data[_pos++];
        }

        std::string fixedString(std::size_t length, std::string_view what) {
            require(length, what);
            const auto begin = _data.begin() + static_cast<std::ptrdiff_t>(_pos);
            const auto end = std::find(begin, begin + static_cast<std::ptrdiff_t>(length), uint8_t{ 0 });
            _pos += length;
            return { begin, end };
        }

        std::string cString(std::string_view what) {
            require(1, what);
            const auto begin = _data.begin() + static_cast<std::ptrdiff_t>(_pos);
            const auto end = std::find(begin, _data.end(), uint8_t{ 0 });
            if (end == _data.end()) {
                throw ParseException(std::format("SAVE.DAT ends inside {} (unterminated string at offset {})", what, _pos));
            }
            std::string text(begin, end);
            _pos += text.size() + 1;
            return text;
        }

        std::vector<int32_t> i32List(std::size_t count, std::string_view what) {
            require(count * 4, what);
            std::vector<int32_t> values;
            values.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                values.push_back(i32(what));
            }
            return values;
        }

        void skip(std::size_t length, std::string_view what) {
            require(length, what);
            _pos += length;
        }

        std::size_t position() const { return _pos; }
        void setPosition(std::size_t pos) { _pos = pos; }
        const std::vector<uint8_t>& data() const { return _data; }

    private:
        const std::vector<uint8_t>& _data;
        std::size_t _pos = 0;
    };

    std::optional<std::vector<uint8_t>> readFile(const std::filesystem::path& path) {
        if (std::error_code ec; !std::filesystem::is_regular_file(path, ec)) {
            return std::nullopt;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // gPartyMemberDescriptionsLength: partyMembersInit counts [Party Member N] sections from 0 that carry a
    // party_member_pid, stopping at the first gap.
    std::optional<std::size_t> partyMemberCount(resource::GameResources& resources) {
        const auto bytes = resources.files().readRawBytes("data/party.txt");
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        std::istringstream in(std::string(bytes->begin(), bytes->end()));
        std::set<std::string, std::less<>> sectionsWithPid;
        std::string section;
        ini::parse(
            in, [&section](std::string_view name) { section = name; },
            [&section, &sectionsWithPid](std::string_view key, std::string_view) {
                if (key == "party_member_pid") {
                    sectionsWithPid.insert(section);
                }
            });
        std::size_t count = 0;
        while (sectionsWithPid.contains("Party Member " + std::to_string(count))) {
            ++count;
        }
        return count;
    }

    std::string protoName(resource::GameResources& resources, uint32_t pid) {
        try {
            if (const Pro* pro = resources.loadPro(pid); pro != nullptr) {
                if (Msg* msg = ProHelper::msgFile(resources, pro->type()); msg != nullptr) {
                    return msg->message(pro->header.message_id).text;
                }
            }
        } catch (const std::exception& e) {
            // Leave the name empty, as proto_info does, rather than invent one.
            spdlog::debug("describe_save: no display name for pid {}: {}", pid, e.what());
        }
        return {};
    }

    ordered_json critterJson(resource::GameResources& resources, const MapObject& critter, bool isDude) {
        return { { "who", isDude ? std::string("dude") : protoName(resources, critter.pro_pid) },
            { "pid", std::format("0x{:08X}", critter.pro_pid) }, { "objectId", critter.unknown0 },
            { "hex", critter.position }, { "col", critter.position % 200 }, { "row", critter.position / 200 },
            { "team", critter.group_id }, { "aiPacket", critter.ai_packet },
            { "hp", static_cast<int32_t>(critter.current_hp) }, { "ap", static_cast<int32_t>(critter.current_ap) },
            { "damageLastTurn", static_cast<int32_t>(critter.damage_last_turn) },
            { "maneuverFlags", flagNames(critter.maneuver, kCritterManeuverFlags) },
            { "resultFlags", flagNames(critter.combat_results, kDamFlags) },
            { "whoHitMeCid", static_cast<int32_t>(critter.who_hit_me) }, { "scriptProgramIndex", critter.script_id } };
    }

    struct SaveHeader {
        ordered_json json;
        std::string mapFile;
        uint32_t elevation = 0;
    };

    // lsgSaveHeaderInSlot
    SaveHeader readHeader(SaveReader& r) {
        if (const std::string signature = r.fixedString(24, "signature"); signature != LOAD_SAVE_SIGNATURE) {
            throw ParseException(std::format("not a Fallout save (signature '{}')", signature));
        }
        SaveHeader header;
        ordered_json& json = header.json;
        const int16_t versionFirst = r.i16("version");
        const int16_t versionSecond = r.i16("version");
        json["version"] = { versionFirst, versionSecond };
        json["release"] = r.u8("release");
        json["characterName"] = r.fixedString(32, "character name");
        json["description"] = r.fixedString(30, "description");
        const int16_t fileDay = r.i16("file date");
        const int16_t fileMonth = r.i16("file date");
        const int16_t fileYear = r.i16("file date");
        json["savedOn"] = std::format("{:04}-{:02}-{:02}", fileYear, fileMonth, fileDay);
        r.i32("file time");
        const int16_t gameMonth = r.i16("game date");
        const int16_t gameDay = r.i16("game date");
        const int16_t gameYear = r.i16("game date");
        json["gameDate"] = std::format("{:04}-{:02}-{:02}", gameYear, gameMonth, gameDay);
        json["gameTime"] = static_cast<uint32_t>(r.i32("game time"));
        header.elevation = static_cast<uint32_t>(r.i16("elevation"));
        json["elevation"] = header.elevation;
        json["mapIndex"] = r.i16("map index");
        header.mapFile = r.fixedString(16, "map file name");
        json["mapFile"] = header.mapFile;
        r.skip(LS_PREVIEW_SIZE, "preview image");
        r.skip(128, "header padding");
        return header;
    }

    struct GlobalsAndMaps {
        std::vector<int32_t> globals;
        ordered_json maps;
    };

    // scriptsSaveGameGlobalVars, _GameMap2Slot (the slot's map list, then the AUTOMAP.DB size), then the
    // second copy of the globals the engine writes, which is also the alignment check.
    GlobalsAndMaps readGlobalsAndMaps(SaveReader& r, std::size_t globalCount) {
        GlobalsAndMaps result;
        result.globals = r.i32List(globalCount, "global variables");
        const int32_t mapCount = r.i32("map list length");
        if (mapCount < 0 || mapCount > MAX_MAP_LIST) {
            throw ParseException(std::format("implausible map list length {} after {} global variables: data/vault13.gam "
                                             "does not match the one this save was written with",
                mapCount, globalCount));
        }
        auto maps = ordered_json::array();
        for (int32_t i = 0; i < mapCount; ++i) {
            maps.push_back(r.cString("map list"));
        }
        const int32_t automapSize = r.i32("automap size");
        if (r.i32List(globalCount, "second global variable copy") != result.globals) {
            throw ParseException(std::format("the two global-variable copies differ: data/vault13.gam ({} variables) "
                                             "does not match the one this save was written with",
                globalCount));
        }
        result.maps = { { "inSlot", std::move(maps) }, { "automapDbSize", automapSize } };
        return result;
    }

    ordered_json changedGlobals(const std::vector<int32_t>& globals, const GlobalDefaults& defaults) {
        auto changed = ordered_json::array();
        for (std::size_t i = 0; i < globals.size(); ++i) {
            if (globals[i] != defaults[i].second) {
                changed.push_back({ { "index", i }, { "name", defaults[i].first }, { "value", globals[i] },
                    { "default", defaults[i].second } });
            }
        }
        return changed;
    }

    struct PlayerRecord {
        std::unique_ptr<MapObject> object;
        ordered_json json;
    };

    // _obj_save_dude (the player object, then gCenterTile) and critterSave (_sneak_working, then the player's
    // CritterProtoData).
    PlayerRecord readPlayer(SaveReader& r, resource::GameResources& resources, int32_t cid) {
        PlayerRecord player;
        MapReader objectReader(makeProtoLoader(resources));
        std::size_t afterObject = 0;
        player.object = objectReader.readObjectAt(r.data(), r.position(), afterObject);
        if (player.object->pro_pid != DUDE_PID) {
            throw ParseException(std::format("expected the player object (pid 0x{:08X}) at offset {}, found pid 0x{:08X}",
                DUDE_PID, r.position(), player.object->pro_pid));
        }
        r.setPosition(afterObject);
        const int32_t centerTile = r.i32("view centre tile");
        const int32_t sneakWorking = r.i32("sneak state");
        const auto protoFlags = static_cast<uint32_t>(r.i32("player proto flags"));
        const auto baseStats = r.i32List(SAVEABLE_STAT_COUNT, "base stats");
        const auto bonusStats = r.i32List(SAVEABLE_STAT_COUNT, "bonus stats");
        const auto skills = r.i32List(SKILL_COUNT, "skills");
        const int32_t bodyType = r.i32("body type");
        const int32_t experience = r.i32("experience");
        r.i32("kill type");
        r.i32("damage type");

        player.json = critterJson(resources, *player.object, true);
        player.json["cid"] = cid;
        player.json["inventoryItems"] = player.object->objects_in_inventory;
        player.json["viewCentreTile"] = centerTile;
        player.json["sneaking"] = (protoFlags & CRITTER_DUDE_SNEAKING) != 0;
        player.json["sneakWorking"] = sneakWorking != 0;
        player.json["experience"] = experience;
        player.json["bodyType"] = bodyType;
        // Stats and skills are indexed by the engine's STAT_* and SKILL_* numbers.
        player.json["baseStats"] = baseStats;
        player.json["bonusStats"] = bonusStats;
        player.json["skills"] = skills;
        return player;
    }

    // killsSave, skillsSave (tagged skills), randomSave (writes nothing) and perksSave. Kill counts are indexed by
    // the engine's KillType and perks by its Perk number; only the player's perk row is reported.
    void readKillsSkillsPerks(SaveReader& r, std::size_t partyCount, ordered_json& root) {
        root["killsByType"] = r.i32List(KILL_TYPE_DEFAULT_COUNT, "kill counts");
        root["taggedSkills"] = r.i32List(NUM_TAGGED_SKILLS, "tagged skills");
        const auto perkRanks = r.i32List(partyCount * PERK_COUNT, "perk ranks");
        auto playerPerks = ordered_json::array();
        for (std::size_t perk = 0; perk < PERK_COUNT; ++perk) {
            if (perkRanks[perk] != 0) {
                playerPerks.push_back({ { "perk", perk }, { "rank", perkRanks[perk] } });
            }
        }
        root["perks"] = std::move(playerPerks);
    }

    bool isHidden(const MapObject& object) {
        return (object.flags & static_cast<uint32_t>(Pro::ObjectFlags::OBJECT_HIDDEN)) != 0;
    }

    // The slot's copy of the current map, indexed for joining the combat block the way combatLoad does. It rebuilds
    // the combat list with objectListCreate(-1, gElevation, OBJ_TYPE_CRITTER), which leaves out OBJECT_HIDDEN
    // critters and other elevations, and finds each saved cid with _find_cid: the first match in tile order, the
    // order objectSaveAll wrote the map in. A critter left out keeps whatever cid it last had, which can duplicate a
    // listed one, so those are never searched and are reported apart.
    class SlotMapIndex {
    public:
        SlotMapIndex(Map* map, const MapObject& player, uint32_t elevation)
            : _mapLoaded(map != nullptr) {
            _objectById.try_emplace(player.unknown0, &player);
            if (map == nullptr) {
                return;
            }
            // map_objects is unordered; walk the elevations in file order.
            const auto& objectsByElevation = map->getMapFile().map_objects;
            for (int mapElevation = 0; mapElevation < ELEVATION_COUNT; ++mapElevation) {
                const auto it = objectsByElevation.find(mapElevation);
                if (it == objectsByElevation.end()) {
                    continue;
                }
                for (const auto& object : it->second) {
                    if (object) {
                        add(*object, elevation);
                    }
                }
            }
        }

        bool mapLoaded() const {
            return _mapLoaded;
        }

        const MapObject* critter(int32_t cid) const {
            const auto it = std::ranges::find_if(_listed, [cid](const MapObject* listed) { return listed->critter_index == cid; });
            return it != _listed.end() ? *it : nullptr;
        }

        // The map critters combatLoad lists again; the player is not among them (its record is in SAVE.DAT).
        std::size_t listedCount() const {
            return _listed.size();
        }

        const std::vector<const MapObject*>& unlisted() const {
            return _unlisted;
        }

        ordered_json objectRef(int32_t id) const {
            if (id < 0) {
                return nullptr;
            }
            ordered_json ref = { { "objectId", id } };
            if (const auto it = _objectById.find(static_cast<uint32_t>(id)); it != _objectById.end()) {
                ref["pid"] = std::format("0x{:08X}", it->second->pro_pid);
                ref["cid"] = it->second->critter_index;
                ref["hex"] = it->second->position;
            }
            return ref;
        }

    private:
        void add(const MapObject& object, uint32_t elevation) {
            _objectById.try_emplace(object.unknown0, &object);
            if (object.objectType() != static_cast<uint32_t>(Pro::OBJECT_TYPE::CRITTER)) {
                return;
            }
            if (isHidden(object) || object.elevation != elevation) {
                _unlisted.push_back(&object);
            } else {
                _listed.push_back(&object);
            }
        }

        bool _mapLoaded;
        std::vector<const MapObject*> _listed;
        std::vector<const MapObject*> _unlisted;
        std::map<uint32_t, const MapObject*> _objectById;
    };

    struct CombatContext {
        SaveReader& reader;
        resource::GameResources& resources;
        const MapObject& player;
        const SlotMapIndex& index;
    };

    // One combat-list entry: the critter by its combat id, then its CombatAiInfo from the per-entry block.
    ordered_json combatEntry(const CombatContext& ctx, int32_t position, int32_t cid, int32_t combatants, int32_t playerCid) {
        const int32_t friendlyDeadId = ctx.reader.i32("combat AI info");
        const int32_t lastTargetId = ctx.reader.i32("combat AI info");
        const int32_t lastItemId = ctx.reader.i32("combat AI info");
        const int32_t lastMove = ctx.reader.i32("combat AI info");

        ordered_json entry = { { "position", position }, { "cid", cid },
            { "partition", position < combatants ? "combatant" : "noncombatant" } };
        if (cid == playerCid) {
            entry["critter"] = critterJson(ctx.resources, ctx.player, true);
        } else if (const MapObject* critter = ctx.index.critter(cid); critter != nullptr) {
            entry["critter"] = critterJson(ctx.resources, *critter, false);
        } else {
            entry["critter"] = nullptr;
        }
        entry["aiInfo"] = { { "friendlyDead", ctx.index.objectRef(friendlyDeadId) },
            { "lastTarget", ctx.index.objectRef(lastTargetId) }, { "lastItemId", lastItemId }, { "lastMove", lastMove } };
        return entry;
    }

    // The critters combatLoad leaves out of the list. It relinks whoHitMe from whoHitMeCid only for listed critters,
    // so these keep the raw id in the pointer, and objectPrepareWhoHitMeForSave follows it on the next in-combat
    // save unless the critter's maneuver is CRITTER_MANEUVER_NONE.
    ordered_json unlistedCritters(const CombatContext& ctx) {
        auto array = ordered_json::array();
        for (const MapObject* critter : ctx.index.unlisted()) {
            ordered_json entry = critterJson(ctx.resources, *critter, false);
            entry["cid"] = critter->critter_index;
            entry["elevation"] = critter->elevation;
            entry["hidden"] = isHidden(*critter);
            entry["whoHitMeReadOnCombatSave"] = critter->maneuver != 0;
            array.push_back(std::move(entry));
        }
        return array;
    }

    // combatSave: the state, then in combat the counters, the combat list in turn order and each entry's AI info.
    ordered_json readCombat(const CombatContext& ctx, std::size_t partyCount) {
        SaveReader& r = ctx.reader;
        const auto state = static_cast<uint32_t>(r.i32("combat state"));
        if ((state & ~COMBAT_STATE_KNOWN) != 0) {
            throw ParseException(std::format("combat state 0x{:X} is not a combat state: data/party.txt ({} party members) "
                                             "does not match the data this save was written with",
                state, partyCount));
        }
        ordered_json combat = { { "state", state }, { "inCombat", (state & COMBAT_STATE_IN_COMBAT) != 0 },
            { "playerTurn", (state & COMBAT_STATE_PLAYER_TURN) != 0 },
            { "exitRequested", (state & COMBAT_STATE_EXIT_REQUESTED) != 0 } };
        if ((state & COMBAT_STATE_IN_COMBAT) == 0) {
            return combat;
        }

        const int32_t turnRunning = r.i32("combat turn running");
        const int32_t freeMove = r.i32("combat free move");
        const int32_t experience = r.i32("combat experience");
        const int32_t combatants = r.i32("combatant count");
        const int32_t noncombatants = r.i32("non-combatant count");
        const int32_t total = r.i32("combat list length");
        const int32_t playerCid = r.i32("player combat id");
        if (combatants < 0 || noncombatants < 0 || total < 0 || total > MAX_COMBAT_LIST || combatants + noncombatants != total) {
            throw ParseException(std::format("inconsistent combat list sizes: {} combatants + {} non-combatants, list of {}",
                combatants, noncombatants, total));
        }
        const auto cids = r.i32List(static_cast<std::size_t>(total), "combat list");
        auto list = ordered_json::array();
        for (int32_t i = 0; i < total; ++i) {
            list.push_back(combatEntry(ctx, i, cids[static_cast<std::size_t>(i)], combatants, playerCid));
        }
        combat["turnRunning"] = turnRunning != 0;
        combat["freeMove"] = freeMove;
        combat["experience"] = experience;
        combat["combatants"] = combatants;
        combat["noncombatants"] = noncombatants;
        combat["playerCid"] = playerCid;
        combat["list"] = std::move(list);
        if (ctx.index.mapLoaded()) {
            // combatLoad refuses the save unless the list it rebuilds (the listed critters and the player) has the
            // saved length.
            const std::size_t reloaded = ctx.index.listedCount() + 1;
            combat["reloadedListLength"] = reloaded;
            combat["listLengthMatches"] = std::cmp_equal(reloaded, total);
            combat["outsideCombatList"] = unlistedCritters(ctx);
        }
        return combat;
    }

    ordered_json decodeSave(resource::GameResources& resources, const std::vector<uint8_t>& bytes,
        const std::filesystem::path& slot, const GlobalDefaults& globalDefaults, std::size_t partyCount) {
        SaveReader r(bytes);
        const SaveHeader header = readHeader(r);
        ordered_json root = { { "slot", slot.string() }, { "header", header.json } };

        const int32_t playerCid = r.i32("player combat id");
        const GlobalsAndMaps globalsAndMaps = readGlobalsAndMaps(r, globalDefaults.size());
        root["globals"] = { { "count", globalsAndMaps.globals.size() },
            { "changed", changedGlobals(globalsAndMaps.globals, globalDefaults) } };
        root["maps"] = globalsAndMaps.maps;

        const PlayerRecord player = readPlayer(r, resources, playerCid);
        root["dude"] = player.json;
        readKillsSkillsPerks(r, partyCount, root);

        const std::filesystem::path currentMapPath = slot / header.mapFile;
        std::string mapError;
        const auto currentMap = loadMap(resources, currentMapPath.string(), &mapError);
        root["currentMap"] = { { "path", currentMapPath.string() }, { "loaded", currentMap != nullptr } };
        if (!currentMap) {
            root["currentMap"]["error"] = mapError;
        }
        const SlotMapIndex index(currentMap.get(), *player.object, header.elevation);
        root["combat"] = readCombat(CombatContext{ r, resources, *player.object, index }, partyCount);
        root["parsedBytes"] = r.position();
        root["totalBytes"] = bytes.size();
        return root;
    }

} // namespace

int describeSave(resource::GameResources& resources, const DescribeSaveOptions& options, std::ostream& out) {
    std::filesystem::path slot = options.slotPath;
    std::filesystem::path saveDat = slot;
    if (std::error_code ec; std::filesystem::is_directory(slot, ec)) {
        saveDat = slot / "SAVE.DAT";
    } else {
        slot = slot.parent_path();
    }
    const auto bytes = readFile(saveDat);
    if (!bytes.has_value()) {
        out << "describe_save: cannot read " << saveDat.string() << "\n";
        return 1;
    }
    const Gam* gam = loadGameGam(resources);
    if (gam == nullptr) {
        out << "describe_save: data/vault13.gam is not mounted; the save's global-variable blocks have its length\n";
        return 1;
    }
    const auto partyCount = partyMemberCount(resources);
    if (!partyCount.has_value() || *partyCount == 0) {
        out << "describe_save: data/party.txt is not mounted or has no [Party Member 0] with a party_member_pid; the "
               "save's perk block has one row per party member\n";
        return 1;
    }

    try {
        const ordered_json root = decodeSave(resources, *bytes, slot, gam->gameGlobalVars(), *partyCount);
        // Names in a save are CP-1252 game text, so emit invalid UTF-8 as replacement characters rather than throwing.
        out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
        return 0;
    } catch (const std::runtime_error& e) {
        out << "describe_save: " << saveDat.string() << ": " << e.what() << "\n";
        return 1;
    }
}

} // namespace geck::cli
