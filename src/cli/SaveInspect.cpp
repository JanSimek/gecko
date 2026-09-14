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
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <algorithm>
#include <cstddef>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    // Engine layout constants (fallout2-ce). They are code, not data, so they are cited rather than loaded.
    constexpr const char* LOAD_SAVE_SIGNATURE = "FALLOUT SAVE FILE"; // loadsave.cc, compared on 18 bytes
    constexpr std::size_t SIGNATURE_COMPARE_LENGTH = 18;
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

    class SaveReader {
    public:
        explicit SaveReader(const std::vector<uint8_t>& data)
            : _data(data) {
        }

        void require(std::size_t length, const std::string& what) const {
            if (_pos > _data.size() || length > _data.size() - _pos) {
                throw std::runtime_error(std::format("SAVE.DAT ends inside {} (offset {}, need {} bytes, file is {})",
                    what, _pos, length, _data.size()));
            }
        }

        int32_t i32(const std::string& what) {
            require(4, what);
            const auto v = static_cast<int32_t>((uint32_t{ _data[_pos] } << 24) | (uint32_t{ _data[_pos + 1] } << 16)
                | (uint32_t{ _data[_pos + 2] } << 8) | uint32_t{ _data[_pos + 3] });
            _pos += 4;
            return v;
        }

        int16_t i16(const std::string& what) {
            require(2, what);
            const auto v = static_cast<int16_t>((_data[_pos] << 8) | _data[_pos + 1]);
            _pos += 2;
            return v;
        }

        uint8_t u8(const std::string& what) {
            require(1, what);
            return _data[_pos++];
        }

        std::string fixedString(std::size_t length, const std::string& what) {
            require(length, what);
            const auto begin = _data.begin() + static_cast<std::ptrdiff_t>(_pos);
            const auto end = std::find(begin, begin + static_cast<std::ptrdiff_t>(length), uint8_t{ 0 });
            _pos += length;
            return { begin, end };
        }

        std::string cString(const std::string& what) {
            require(1, what);
            const auto begin = _data.begin() + static_cast<std::ptrdiff_t>(_pos);
            const auto end = std::find(begin, _data.end(), uint8_t{ 0 });
            if (end == _data.end()) {
                throw std::runtime_error(std::format("SAVE.DAT ends inside {} (unterminated string at offset {})", what, _pos));
            }
            std::string text(begin, end);
            _pos += text.size() + 1;
            return text;
        }

        std::vector<int32_t> i32List(std::size_t count, const std::string& what) {
            require(count * 4, what);
            std::vector<int32_t> values;
            values.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                values.push_back(i32(what));
            }
            return values;
        }

        void skip(std::size_t length, const std::string& what) {
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
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
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
        if (!bytes) {
            return std::nullopt;
        }
        std::istringstream in(std::string(bytes->begin(), bytes->end()));
        std::set<std::string> sectionsWithPid;
        std::string section;
        ini::parse(
            in, [&](const std::string& name) { section = name; },
            [&](const std::string& key, const std::string&) {
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
        } catch (const std::exception&) {
            // Leave the name empty, as proto_info does, rather than invent one.
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
            { "whoHitMeCid", static_cast<int32_t>(critter.who_hit_me) },
            { "scriptProgramIndex", critter.script_id } };
    }

} // namespace

int describeSave(resource::GameResources& resources, const DescribeSaveOptions& options, std::ostream& out) {
    std::filesystem::path slot = options.slotPath;
    std::filesystem::path saveDat = slot;
    std::error_code ec;
    if (std::filesystem::is_directory(slot, ec)) {
        saveDat = slot / "SAVE.DAT";
    } else {
        slot = slot.parent_path();
    }
    const auto bytes = readFile(saveDat);
    if (!bytes) {
        out << "describe_save: cannot read " << saveDat.string() << "\n";
        return 1;
    }

    Gam* gam = loadGameGam(resources);
    if (gam == nullptr) {
        out << "describe_save: data/vault13.gam is not mounted; the save's global-variable blocks have its length\n";
        return 1;
    }
    const auto globalDefaults = gam->gameGlobalVars();
    const auto partyCount = partyMemberCount(resources);
    if (!partyCount) {
        out << "describe_save: data/party.txt is not mounted; the save's perk block has one row per party member\n";
        return 1;
    }

    ordered_json root;
    try {
        SaveReader r(*bytes);

        // lsgSaveHeaderInSlot
        const std::string signature = r.fixedString(24, "signature");
        if (signature.compare(0, SIGNATURE_COMPARE_LENGTH, std::string(LOAD_SAVE_SIGNATURE).substr(0, SIGNATURE_COMPARE_LENGTH)) != 0) {
            out << "describe_save: " << saveDat.string() << " is not a Fallout save (signature '" << signature << "')\n";
            return 1;
        }
        ordered_json header;
        header["version"] = { r.i16("version"), r.i16("version") };
        header["release"] = r.u8("release");
        header["characterName"] = r.fixedString(32, "character name");
        header["description"] = r.fixedString(30, "description");
        const int16_t fileDay = r.i16("file date");
        const int16_t fileMonth = r.i16("file date");
        const int16_t fileYear = r.i16("file date");
        header["savedOn"] = std::format("{:04}-{:02}-{:02}", fileYear, fileMonth, fileDay);
        r.i32("file time");
        const int16_t gameMonth = r.i16("game date");
        const int16_t gameDay = r.i16("game date");
        const int16_t gameYear = r.i16("game date");
        header["gameDate"] = std::format("{:04}-{:02}-{:02}", gameYear, gameMonth, gameDay);
        header["gameTime"] = static_cast<uint32_t>(r.i32("game time"));
        header["elevation"] = r.i16("elevation");
        header["mapIndex"] = r.i16("map index");
        const std::string mapFile = r.fixedString(16, "map file name");
        header["mapFile"] = mapFile;
        r.skip(LS_PREVIEW_SIZE, "preview image");
        r.skip(128, "header padding");
        root["slot"] = slot.string();
        root["header"] = std::move(header);

        // _SaveObjDudeCid, then scriptsSaveGameGlobalVars
        const int32_t dudeCid = r.i32("player combat id");
        const auto globals = r.i32List(globalDefaults.size(), "global variables");

        // _GameMap2Slot: the slot's map list, then the AUTOMAP.DB size
        const int32_t mapCount = r.i32("map list length");
        if (mapCount < 0 || mapCount > 100000) {
            throw std::runtime_error(std::format("implausible map list length {} after {} global variables: data/vault13.gam "
                                                 "does not match the one this save was written with",
                mapCount, globalDefaults.size()));
        }
        auto maps = ordered_json::array();
        for (int32_t i = 0; i < mapCount; ++i) {
            maps.push_back(r.cString("map list"));
        }
        const int32_t automapSize = r.i32("automap size");

        // The engine writes the global variables a second time; it is also our alignment check.
        if (r.i32List(globalDefaults.size(), "second global variable copy") != globals) {
            throw std::runtime_error(std::format("the two global-variable copies differ: data/vault13.gam ({} variables) "
                                                 "does not match the one this save was written with",
                globalDefaults.size()));
        }
        auto changed = ordered_json::array();
        for (std::size_t i = 0; i < globals.size(); ++i) {
            if (globals[i] != globalDefaults[i].second) {
                changed.push_back({ { "index", i }, { "name", globalDefaults[i].first }, { "value", globals[i] },
                    { "default", globalDefaults[i].second } });
            }
        }
        root["globals"] = { { "count", globals.size() }, { "changed", std::move(changed) } };
        root["maps"] = { { "inSlot", std::move(maps) }, { "automapDbSize", automapSize } };

        // _obj_save_dude: the player object, then gCenterTile
        MapReader objectReader(makeProtoLoader(resources));
        std::size_t afterDude = 0;
        const auto dude = objectReader.readObjectAt(r.data(), r.position(), afterDude);
        if (dude->pro_pid != DUDE_PID) {
            throw std::runtime_error(std::format("expected the player object (pid 0x{:08X}) at offset {}, found pid 0x{:08X}",
                DUDE_PID, r.position(), dude->pro_pid));
        }
        r.setPosition(afterDude);
        const int32_t centerTile = r.i32("view centre tile");

        // critterSave: _sneak_working, then the player's CritterProtoData
        const int32_t sneakWorking = r.i32("sneak state");
        const auto protoFlags = static_cast<uint32_t>(r.i32("player proto flags"));
        const auto baseStats = r.i32List(SAVEABLE_STAT_COUNT, "base stats");
        const auto bonusStats = r.i32List(SAVEABLE_STAT_COUNT, "bonus stats");
        const auto skills = r.i32List(SKILL_COUNT, "skills");
        const int32_t bodyType = r.i32("body type");
        const int32_t experience = r.i32("experience");
        r.i32("kill type");
        r.i32("damage type");

        ordered_json dudeJson = critterJson(resources, *dude, true);
        dudeJson["cid"] = dudeCid;
        dudeJson["inventoryItems"] = dude->objects_in_inventory;
        dudeJson["viewCentreTile"] = centerTile;
        dudeJson["sneaking"] = (protoFlags & CRITTER_DUDE_SNEAKING) != 0;
        dudeJson["sneakWorking"] = sneakWorking != 0;
        dudeJson["experience"] = experience;
        dudeJson["bodyType"] = bodyType;
        dudeJson["baseStats"] = baseStats;   // index = engine STAT_*
        dudeJson["bonusStats"] = bonusStats; // index = engine STAT_*
        dudeJson["skills"] = skills;         // index = engine SKILL_*
        root["dude"] = std::move(dudeJson);

        // killsSave, skillsSave (tagged), randomSave (writes nothing), perksSave
        root["killsByType"] = r.i32List(KILL_TYPE_DEFAULT_COUNT, "kill counts"); // index = engine KillType
        root["taggedSkills"] = r.i32List(NUM_TAGGED_SKILLS, "tagged skills");
        const auto perkRanks = r.i32List(*partyCount * PERK_COUNT, "perk ranks");
        auto dudePerks = ordered_json::array();
        for (std::size_t perk = 0; perk < PERK_COUNT; ++perk) {
            if (perkRanks[perk] != 0) {
                dudePerks.push_back({ { "perk", perk }, { "rank", perkRanks[perk] } });
            }
        }
        root["perks"] = std::move(dudePerks); // perk = engine Perk index

        // combatSave
        const auto combatState = static_cast<uint32_t>(r.i32("combat state"));
        if ((combatState & ~COMBAT_STATE_KNOWN) != 0) {
            throw std::runtime_error(std::format("combat state 0x{:X} is not a combat state: data/party.txt ({} party members) "
                                                 "does not match the data this save was written with",
                combatState, *partyCount));
        }
        ordered_json combat = { { "state", combatState }, { "inCombat", (combatState & COMBAT_STATE_IN_COMBAT) != 0 },
            { "playerTurn", (combatState & COMBAT_STATE_PLAYER_TURN) != 0 },
            { "exitRequested", (combatState & COMBAT_STATE_EXIT_REQUESTED) != 0 } };

        // The slot's copy of the current map carries every critter's combat id and live combat state.
        const std::filesystem::path currentMapPath = slot / mapFile;
        std::string mapError;
        const auto currentMap = loadMap(resources, currentMapPath.string(), &mapError);
        root["currentMap"] = { { "path", currentMapPath.string() }, { "loaded", currentMap != nullptr } };
        if (!currentMap) {
            root["currentMap"]["error"] = mapError;
        }
        std::map<int32_t, const MapObject*> critterByCid;
        std::map<uint32_t, const MapObject*> objectById;
        if (currentMap) {
            for (const auto& [elevation, objects] : currentMap->getMapFile().map_objects) {
                for (const auto& object : objects) {
                    if (!object) {
                        continue;
                    }
                    objectById.emplace(object->unknown0, object.get());
                    if (object->objectType() == static_cast<uint32_t>(Pro::OBJECT_TYPE::CRITTER) && object->critter_index >= 0) {
                        critterByCid.emplace(object->critter_index, object.get());
                    }
                }
            }
        }
        objectById.emplace(dude->unknown0, dude.get());

        if ((combatState & COMBAT_STATE_IN_COMBAT) != 0) {
            const int32_t turnRunning = r.i32("combat turn running");
            const int32_t freeMove = r.i32("combat free move");
            const int32_t experienceSoFar = r.i32("combat experience");
            const int32_t listCom = r.i32("combatant count");
            const int32_t listNoncom = r.i32("non-combatant count");
            const int32_t listTotal = r.i32("combat list length");
            const int32_t combatDudeCid = r.i32("player combat id");
            if (listCom < 0 || listNoncom < 0 || listTotal < 0 || listCom + listNoncom != listTotal) {
                throw std::runtime_error(std::format("inconsistent combat list sizes: {} combatants + {} non-combatants != {}",
                    listCom, listNoncom, listTotal));
            }
            const auto cids = r.i32List(static_cast<std::size_t>(listTotal), "combat list");

            auto list = ordered_json::array();
            for (int32_t i = 0; i < listTotal; ++i) {
                const int32_t friendlyDeadId = r.i32("combat AI info");
                const int32_t lastTargetId = r.i32("combat AI info");
                const int32_t lastItemId = r.i32("combat AI info");
                const int32_t lastMove = r.i32("combat AI info");

                const int32_t cid = cids[static_cast<std::size_t>(i)];
                ordered_json entry = { { "position", i }, { "cid", cid },
                    { "partition", i < listCom ? "combatant" : "noncombatant" } };
                if (cid == combatDudeCid) {
                    entry["critter"] = critterJson(resources, *dude, true);
                } else if (const auto it = critterByCid.find(cid); it != critterByCid.end()) {
                    entry["critter"] = critterJson(resources, *it->second, false);
                } else {
                    entry["critter"] = nullptr;
                }
                auto idRef = [&](int32_t id) -> ordered_json {
                    if (id < 0) {
                        return nullptr;
                    }
                    ordered_json ref = { { "objectId", id } };
                    if (const auto it = objectById.find(static_cast<uint32_t>(id)); it != objectById.end()) {
                        ref["pid"] = std::format("0x{:08X}", it->second->pro_pid);
                        ref["cid"] = it->second->critter_index;
                        ref["hex"] = it->second->position;
                    }
                    return ref;
                };
                entry["aiInfo"] = { { "friendlyDead", idRef(friendlyDeadId) }, { "lastTarget", idRef(lastTargetId) },
                    { "lastItemId", lastItemId }, { "lastMove", lastMove } };
                list.push_back(std::move(entry));
            }
            combat["turnRunning"] = turnRunning != 0;
            combat["freeMove"] = freeMove;
            combat["experience"] = experienceSoFar;
            combat["combatants"] = listCom;
            combat["noncombatants"] = listNoncom;
            combat["playerCid"] = combatDudeCid;
            combat["list"] = std::move(list);
        }
        root["combat"] = std::move(combat);
        root["parsedBytes"] = r.position();
        root["totalBytes"] = bytes->size();
    } catch (const std::exception& e) {
        out << "describe_save: " << e.what() << "\n";
        return 1;
    }

    out << root.dump(2) << "\n";
    return 0;
}

} // namespace geck::cli
