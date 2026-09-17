#include "cli/ProtoExport.h"

#include "format/lst/Lst.h"
#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "util/FalloutEngineEnums.h"
#include "util/ProHelper.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace geck::cli {

namespace {
    using nlohmann::ordered_json;

    // JSON identifiers, in the engine's enum order. These are keys, not display labels: the display
    // names the game ships come from the .msg files and are reported next to the ids.
    constexpr std::array<std::string_view, 7> kItemTypeKeys{ "armor", "container", "drug", "weapon", "ammo",
        "misc", "key" }; // ItemType
    constexpr std::array<std::string_view, 7> kDamageTypeKeys{ "normal", "laser", "fire", "plasma",
        "electrical", "emp", "explosion" }; // DamageType
    constexpr std::array<std::string_view, 7> kSpecialKeys{ "strength", "perception", "endurance", "charisma",
        "intelligence", "agility", "luck" }; // STAT_STRENGTH .. STAT_LUCK
    // STAT_MAXIMUM_HIT_POINTS .. STAT_BETTER_CRITICALS (fallout2-ce stat_defs.h). Stat 10 is
    // STAT_UNARMED_DAMAGE; gecko's CritterData calls that slot `unused`.
    constexpr std::array<std::string_view, 10> kDerivedKeys{ "maxHitPoints", "maxActionPoints", "armorClass",
        "unarmedDamage", "meleeDamage", "carryWeight", "sequence", "healingRate", "criticalChance",
        "betterCriticals" };
    constexpr std::array<std::string_view, 18> kSkillKeys{ "smallGuns", "bigGuns", "energyWeapons", "unarmed",
        "meleeWeapons", "throwing", "firstAid", "doctor", "sneak", "lockpick", "steal", "traps", "science",
        "repair", "speech", "barter", "gambling", "outdoorsman" }; // skill_defs.h Skill

    // fallout2-ce item.cc: a weapon's extendedFlags nibble (primary = & 0x0F, secondary = & 0xF0 >> 4)
    // indexes the engine's 9-entry _attack_subtype and _attack_anim tables. An index past the end is
    // read out of bounds by the engine, so it is reported without names.
    struct AttackMode {
        std::string_view type;      // _attack_subtype: AttackType
        std::string_view animation; // _attack_anim: AnimationType
    };
    constexpr std::array<AttackMode, 9> kAttackModes{ {
        { "none", "stand" },
        { "unarmed", "throwPunch" },
        { "unarmed", "kickLeg" },
        { "melee", "swing" },
        { "melee", "thrust" },
        { "throw", "throw" },
        { "ranged", "fireSingle" },
        { "ranged", "fireBurst" },
        { "ranged", "fireContinuous" },
    } };

    // SAVEABLE_STAT_COUNT: a critter proto stores base and bonus values for stats 0..34.
    constexpr std::size_t kSaveableStats = 35;
    using StatArray = std::array<std::int32_t, kSaveableStats>;

    std::int32_t s32(std::uint32_t value) {
        return static_cast<std::int32_t>(value);
    }

    // The game text this export names ids with. Each file is optional: a missing one turns its names
    // into null rather than failing the export or substituting a label.
    class Labels {
    public:
        explicit Labels(resource::GameResources& resources)
            : _proto(tryLoad([&] { return ProHelper::protoMsgFile(resources); }))
            , _perk(tryLoad([&] { return ProHelper::perkMsgFile(resources); }))
            , _stat(tryLoad([&] { return ProHelper::statMsgFile(resources); }))
            , _itemNames(tryLoad([&] { return ProHelper::msgFile(resources, Pro::OBJECT_TYPE::ITEM); }))
            , _critterNames(tryLoad([&] { return ProHelper::msgFile(resources, Pro::OBJECT_TYPE::CRITTER); })) {
        }

        // {id, name} for a proto.msg-named id; message ids per fallout2-ce proto.cc protoInit.
        ordered_json damageType(std::int32_t id) const {
            return named(id, _proto, fallout::PROTO_DAMAGE_TYPE_MESSAGE_ID_BASE + id);
        }
        ordered_json caliber(std::int32_t id) const {
            return named(id, _proto, fallout::PROTO_CALIBER_MESSAGE_ID_BASE + id);
        }
        ordered_json material(std::int32_t id) const {
            return named(id, _proto, fallout::PROTO_MATERIAL_MESSAGE_ID_BASE + id);
        }
        ordered_json bodyType(std::int32_t id) const {
            return named(id, _proto, fallout::PROTO_BODY_TYPE_MESSAGE_ID_BASE + id);
        }
        // perk.msg 101 + perk (perk.cc perksInit); -1 means no perk.
        ordered_json perk(std::int32_t id) const {
            return id < 0 ? ordered_json(nullptr) : named(id, _perk, fallout::PERK_NAME_MESSAGE_ID_BASE + id);
        }
        // stat.msg 100 + stat (stat.cc statsInit). A negative id is not a stat — a drug uses -1 for an
        // empty slot and -2 for a randomised amount — so its name is null.
        ordered_json stat(std::int32_t id) const {
            return named(id, _stat, fallout::STAT_NAME_MESSAGE_ID_BASE + id);
        }

        // The proto's own name and examine text: message_id and message_id + 1 in pro_item/pro_crit.msg.
        ordered_json protoText(Pro::OBJECT_TYPE type, std::uint32_t messageId) const {
            return text(type == Pro::OBJECT_TYPE::ITEM ? _itemNames : _critterNames, static_cast<int>(messageId));
        }

    private:
        template <typename Load>
        static const Msg* tryLoad(Load load) {
            try {
                return load();
            } catch (const std::exception&) {
                return nullptr;
            }
        }

        // find() rather than Msg::message(), which inserts an empty entry on a miss.
        static ordered_json text(const Msg* msg, int messageId) {
            if (msg == nullptr) {
                return nullptr;
            }
            const auto& messages = msg->getMessages();
            const auto it = messages.find(messageId);
            return it == messages.end() ? ordered_json(nullptr) : ordered_json(it->second.text);
        }

        static ordered_json named(std::int32_t id, const Msg* msg, int messageId) {
            return { { "id", id }, { "name", id < 0 ? ordered_json(nullptr) : text(msg, messageId) } };
        }

        const Msg* _proto;
        const Msg* _perk;
        const Msg* _stat;
        const Msg* _itemNames;
        const Msg* _critterNames;
    };

    ordered_json damageTable(const std::uint32_t* values) {
        ordered_json table;
        for (std::size_t i = 0; i < kDamageTypeKeys.size(); ++i) {
            table[std::string(kDamageTypeKeys[i])] = s32(values[i]);
        }
        return table;
    }

    ordered_json attackMode(std::uint32_t index) {
        ordered_json mode;
        mode["index"] = index;
        const bool known = index < kAttackModes.size();
        mode["type"] = known ? ordered_json(std::string(kAttackModes[index].type)) : ordered_json(nullptr);
        mode["animation"] = known ? ordered_json(std::string(kAttackModes[index].animation)) : ordered_json(nullptr);
        return mode;
    }

    ordered_json extendedItemFlags(std::uint32_t flags) {
        using enum Pro::ExtendedItemFlags;
        return { { "raw", flags }, { "bigGun", Pro::hasFlag(flags, BIG_GUN) },
            { "twoHanded", Pro::hasFlag(flags, TWO_HANDED) }, { "hiddenItem", Pro::hasFlag(flags, ITEM_HIDDEN) } };
    }

    ordered_json weaponData(const Pro::WeaponData& w, const Labels& labels) {
        ordered_json out;
        out["animationCode"] = w.animationCode;
        out["damage"] = { { "min", s32(w.damageMin) }, { "max", s32(w.damageMax) } };
        out["damageType"] = labels.damageType(s32(w.damageType));
        out["range"] = { { "primary", s32(w.rangePrimary) }, { "secondary", s32(w.rangeSecondary) } };
        out["projectilePid"] = w.projectilePID;
        out["minStrength"] = s32(w.minimumStrength);
        out["apCost"] = { { "primary", s32(w.actionCostPrimary) }, { "secondary", s32(w.actionCostSecondary) } };
        out["criticalFail"] = s32(w.criticalFail);
        out["perk"] = labels.perk(s32(w.perk));
        out["burstRounds"] = s32(w.burstRounds);
        out["caliber"] = labels.caliber(s32(w.ammoType));
        out["ammoPid"] = w.ammoPID;
        out["ammoCapacity"] = s32(w.ammoCapacity);
        out["soundId"] = w.soundId;
        // Not part of the engine's weapon record. fallout2-ce protoItemDataRead stops at the soundCode
        // byte (a 122-byte file), and every shipped weapon proto is exactly that long, so ProReader only
        // ever fills this with its 0 default. sfall's "Energy Weapon" flag is extendedFlags 0x0400
        // (sfall Skills.cpp), not a trailing field. Exported because gecko reads it, and it is 0 for any
        // proto the engine can load.
        out["weaponFlags"] = w.weaponFlags;
        return out;
    }

    // Named for the engine's reading (fallout2-ce ProtoItemAmmoData: ac_adjust, dr_adjust, dam_mult,
    // dam_div), not for the Pro::AmmoData field names.
    ordered_json ammoData(const Pro::AmmoData& a, const Labels& labels) {
        return { { "caliber", labels.caliber(s32(a.caliber)) }, { "quantity", s32(a.quantity) },
            { "acModifier", a.damageModifier }, { "drModifier", a.damageResistModifier },
            { "damageMultiplier", a.damageMultiplier }, { "damageDivisor", a.damageTypeModifier } };
    }

    ordered_json armorData(const Pro::ArmorData& a, const Labels& labels) {
        return { { "ac", s32(a.armorClass) }, { "dr", damageTable(a.damageResist) },
            { "dt", damageTable(a.damageThreshold) }, { "perk", labels.perk(s32(a.perk)) },
            { "maleFid", a.armorMaleFID }, { "femaleFid", a.armorFemaleFID } };
    }

    // fallout2-ce ProtoItemDrugData: stat[3], amount[3], duration1, amount1[3], duration2, amount2[3],
    // addictionChance, withdrawalEffect (a perk), withdrawalOnset. Durations are game minutes.
    ordered_json drugData(const Pro::DrugData& d, const Labels& labels) {
        ordered_json out;
        out["stats"] = { labels.stat(s32(d.stat0)), labels.stat(s32(d.stat1)), labels.stat(s32(d.stat2)) };
        out["immediate"] = { d.amount0, d.amount1, d.amount2 };
        out["delayed1"] = { { "minutes", s32(d.duration1) }, { "amounts", { d.amount0_1, d.amount1_1, d.amount2_1 } } };
        out["delayed2"] = { { "minutes", s32(d.duration2) }, { "amounts", { d.amount0_2, d.amount1_2, d.amount2_2 } } };
        out["addictionChance"] = s32(d.addictionRate);
        out["withdrawalPerk"] = labels.perk(s32(d.addictionEffect));
        out["withdrawalOnset"] = s32(d.addictionOnset);
        return out;
    }

    // One object named after the item type, holding that type's record.
    void addItemTypeData(ordered_json& row, const Pro& pro, const Labels& labels) {
        using enum Pro::ITEM_TYPE;
        const auto key = std::string(kItemTypeKeys[pro.objectSubtypeId()]);
        switch (pro.itemType()) {
            case ARMOR:
                row[key] = armorData(pro.armorData, labels);
                break;
            case CONTAINER:
                row[key] = { { "maxSize", s32(pro.containerData.maxSize) },
                    { "openFlags", pro.containerData.flags } };
                break;
            case DRUG:
                row[key] = drugData(pro.drugData, labels);
                break;
            case WEAPON:
                row[key] = weaponData(pro.weaponData, labels);
                break;
            case AMMO:
                row[key] = ammoData(pro.ammoData, labels);
                break;
            case MISC:
                row[key] = { { "powerTypePid", pro.miscData.powerTypePid },
                    { "powerType", s32(pro.miscData.powerType) }, { "charges", s32(pro.miscData.charges) } };
                break;
            case KEY:
                row[key] = { { "keyCode", s32(pro.keyData.keyId) } };
                break;
        }
    }

    void addItemFields(ordered_json& row, const Pro& pro, const Labels& labels) {
        const auto& common = pro.commonItemData;
        row["extendedFlags"] = extendedItemFlags(common.flagsExt);
        row["itemType"] = std::string(kItemTypeKeys[pro.objectSubtypeId()]);
        row["attackModes"] = { { "primary", attackMode(Pro::getAnimationPrimary(common.flagsExt)) },
            { "secondary", attackMode(Pro::getAnimationSecondary(common.flagsExt)) } };
        row["material"] = labels.material(s32(common.materialId));
        row["size"] = s32(common.containerSize);
        row["weight"] = s32(common.weight);
        row["cost"] = s32(common.basePrice);
        row["inventoryFid"] = common.inventoryFID;
        row["soundId"] = common.soundId;
        addItemTypeData(row, pro, labels);
    }

    // A critter proto's base and bonus blocks are each SAVEABLE_STAT_COUNT int32s indexed by stat id
    // (fallout2-ce protoCritterDataRead). Pro::CritterData spreads them over named fields, so they are
    // flattened back in file order and read by stat id. That matters for the bonus block, whose fields
    // split its sixteen resistances 8 + 8 where the engine's stats are 7 DT + 7 DR + radiation + poison.
    StatArray baseStats(const Pro::CritterData& c) {
        StatArray s{};
        std::size_t i = 0;
        auto put = [&](std::uint32_t value) { s[i++] = s32(value); };
        for (const auto value : c.specialStats) {
            put(value);
        }
        for (const auto value : { c.maxHitPoints, c.actionPoints, c.armorClass, c.unused, c.meleeDamage,
                 c.carryWeightMax, c.sequence, c.healingRate, c.criticalChance, c.betterCriticals }) {
            put(value);
        }
        for (const auto value : c.damageThreshold) {
            put(value);
        }
        for (const auto value : c.damageResist) {
            put(value);
        }
        put(c.age);
        put(c.gender);
        return s;
    }

    StatArray bonusStats(const Pro::CritterData& c) {
        StatArray s{};
        std::size_t i = 0;
        auto put = [&](std::uint32_t value) { s[i++] = s32(value); };
        for (const auto value : c.bonusSpecialStats) {
            put(value);
        }
        for (const auto value : { c.bonusHealthPoints, c.bonusActionPoints, c.bonusArmorClass, c.bonusUnused,
                 c.bonusMeleeDamage, c.bonusCarryWeight, c.bonusSequence, c.bonusHealingRate,
                 c.bonusCriticalChance, c.bonusBetterCriticals }) {
            put(value);
        }
        for (const auto value : c.bonusDamageThreshold) {
            put(value);
        }
        for (const auto value : c.bonusDamageResistance) {
            put(value);
        }
        put(c.bonusAge);
        put(c.bonusGender);
        return s;
    }

    ordered_json statBlock(const StatArray& s) {
        using fallout::StatId;
        const auto at = [&s](StatId id) { return s[static_cast<std::size_t>(id)]; };
        ordered_json special;
        for (std::size_t i = 0; i < kSpecialKeys.size(); ++i) {
            special[std::string(kSpecialKeys[i])] = s[i];
        }
        ordered_json block;
        block["special"] = std::move(special);
        const auto firstDerived = static_cast<std::size_t>(StatId::MaximumHitPoints);
        for (std::size_t i = 0; i < kDerivedKeys.size(); ++i) {
            block[std::string(kDerivedKeys[i])] = s[firstDerived + i];
        }
        ordered_json dt;
        ordered_json dr;
        for (std::size_t i = 0; i < kDamageTypeKeys.size(); ++i) {
            const auto key = std::string(kDamageTypeKeys[i]);
            dt[key] = s[static_cast<std::size_t>(StatId::DamageThreshold) + i];
            dr[key] = s[static_cast<std::size_t>(StatId::DamageResistance) + i];
        }
        block["dt"] = std::move(dt);
        block["dr"] = std::move(dr);
        block["radiationResistance"] = at(StatId::RadiationResistance);
        block["poisonResistance"] = at(StatId::PoisonResistance);
        block["age"] = at(StatId::Age);
        block["gender"] = at(StatId::Gender);
        return block;
    }

    // fallout2-ce obj_types.h CritterFlags. CRITTER_FLAT is named for its effect: a critter with it
    // keeps its corpse upright instead of being flattened (critter.cc critterKill).
    ordered_json critterFlags(std::uint32_t flags) {
        using enum Pro::CritterFlags;
        return { { "raw", flags }, { "barter", Pro::hasFlag(flags, CRITTER_BARTER) },
            { "noSteal", Pro::hasFlag(flags, CRITTER_NO_STEAL) }, { "noDrop", Pro::hasFlag(flags, CRITTER_NO_DROP) },
            { "noLimbs", Pro::hasFlag(flags, CRITTER_NO_LIMBS) }, { "noAge", Pro::hasFlag(flags, CRITTER_NO_AGE) },
            { "noHeal", Pro::hasFlag(flags, CRITTER_NO_HEAL) },
            { "invulnerable", Pro::hasFlag(flags, CRITTER_INVULNERABLE) },
            { "noFlatten", Pro::hasFlag(flags, CRITTER_NO_FLATTEN) },
            { "specialDeath", Pro::hasFlag(flags, CRITTER_SPECIAL_DEATH) },
            { "longLimbs", Pro::hasFlag(flags, CRITTER_LONG_LIMBS) },
            { "noKnockback", Pro::hasFlag(flags, CRITTER_NO_KNOCKBACK) } };
    }

    void addCritterFields(ordered_json& row, const Pro& pro, const Labels& labels) {
        const auto& c = pro.critterData;
        row["extendedFlags"] = { { "raw", pro.commonItemData.flagsExt } };
        row["headFid"] = s32(c.headFID);
        row["aiPacket"] = s32(c.aiPacket);
        row["team"] = s32(c.teamNumber);
        row["critterFlags"] = critterFlags(c.flags);
        row["base"] = statBlock(baseStats(c));
        row["bonus"] = statBlock(bonusStats(c));
        ordered_json skills;
        for (std::size_t i = 0; i < kSkillKeys.size(); ++i) {
            skills[std::string(kSkillKeys[i])] = s32(c.skills[i]);
        }
        row["skills"] = std::move(skills);
        row["bodyType"] = labels.bodyType(s32(c.bodyType));
        row["xp"] = s32(c.experienceForKill);
        row["killType"] = s32(c.killType);
        // The critter's natural (unarmed) damage type. Two shipped protos predate the field and are
        // 412 bytes; the engine and ProReader both read those as normal damage.
        row["damageType"] = labels.damageType(s32(c.damageType));
    }

    // A proto's script: sid & 0xFFFFFF is the 0-based scripts.lst index the engine attaches
    // (proto_instance.cc, script->index = sid & 0xFFFFFF). -1 means none.
    ordered_json scriptOf(std::int32_t sid, const Lst* scriptsLst) {
        if (sid == -1) {
            return nullptr;
        }
        const auto programIndex = static_cast<std::size_t>(sid & 0xFFFFFF);
        ordered_json script;
        script["programIndex"] = programIndex;
        script["name"] = scriptsLst != nullptr && programIndex < scriptsLst->list().size()
            ? ordered_json(scriptsLst->list()[programIndex])
            : ordered_json(nullptr);
        return script;
    }

    const char* kindName(Pro::OBJECT_TYPE kind) {
        return kind == Pro::OBJECT_TYPE::ITEM ? "item" : "critter";
    }

    struct ListSource {
        Pro::OBJECT_TYPE kind;
        std::string_view path;
    };

    class Exporter {
    public:
        Exporter(resource::GameResources& resources, const ProtoExportOptions& options)
            : _resources(resources)
            , _options(options)
            , _labels(resources) {
            try {
                _scriptsLst = resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
            } catch (const std::exception&) {
                _scriptsLst = nullptr; // no script names; the sid is still reported
            }
        }

        // Returns false (with `error` set) when the list itself cannot be read.
        bool walk(const ListSource& source, std::string& error) {
            const Lst* lst = nullptr;
            try {
                lst = _resources.repository().load<Lst>(source.path);
            } catch (const std::exception& e) {
                error = std::string(source.path) + ": " + e.what();
                return false;
            }
            const auto& entries = lst->list();
            _lists.push_back({ { "kind", kindName(source.kind) }, { "path", std::string(source.path) },
                { "entries", entries.size() } });
            // PIDs are 1-based lines of the .lst (proto.cc _proto_list_str).
            for (std::size_t line = 1; line <= entries.size(); ++line) {
                addProto(source.kind, Pro::makePid(source.kind, static_cast<std::uint32_t>(line)), entries[line - 1]);
            }
            return true;
        }

        ordered_json result() {
            ordered_json root;
            root["lists"] = std::move(_lists);
            root["protoCount"] = _protos.size();
            root["unreadable"] = std::move(_unreadable);
            root["protos"] = std::move(_protos);
            return root;
        }

    private:
        void addProto(Pro::OBJECT_TYPE kind, std::uint32_t pid, const std::string& file) {
            const Pro* pro = nullptr;
            try {
                pro = _resources.loadPro(pid);
            } catch (const std::exception& e) {
                _unreadable.push_back({ { "pid", pid }, { "kind", kindName(kind) }, { "file", file },
                    { "reason", e.what() } });
                return;
            }
            if (pro == nullptr) {
                _unreadable.push_back({ { "pid", pid }, { "kind", kindName(kind) }, { "file", file },
                    { "reason", "not loaded" } });
                return;
            }
            if (kind == Pro::OBJECT_TYPE::ITEM && pro->objectSubtypeId() >= kItemTypeKeys.size()) {
                _unreadable.push_back({ { "pid", pid }, { "kind", kindName(kind) }, { "file", file },
                    { "reason", "unknown item type " + std::to_string(pro->objectSubtypeId()) } });
                return;
            }
            if (kind == Pro::OBJECT_TYPE::ITEM && !matchesItemType(*pro)) {
                return;
            }
            _protos.push_back(row(kind, pid, file, *pro));
        }

        bool matchesItemType(const Pro& pro) const {
            return !_options.itemType.has_value() || pro.itemType() == *_options.itemType;
        }

        ordered_json row(Pro::OBJECT_TYPE kind, std::uint32_t pid, const std::string& file, const Pro& pro) const {
            ordered_json row;
            row["pid"] = pid;
            row["kind"] = kindName(kind);
            row["file"] = file;
            row["name"] = _labels.protoText(kind, pro.header.message_id);
            row["description"] = _labels.protoText(kind, pro.header.message_id + 1);
            row["fid"] = pro.header.FID;
            row["flags"] = pro.header.flags;
            const auto sid = s32(pro.commonItemData.SID);
            row["sid"] = sid;
            row["script"] = scriptOf(sid, _scriptsLst);
            if (kind == Pro::OBJECT_TYPE::ITEM) {
                addItemFields(row, pro, _labels);
            } else {
                addCritterFields(row, pro, _labels);
            }
            return row;
        }

        resource::GameResources& _resources;
        const ProtoExportOptions& _options;
        Labels _labels;
        const Lst* _scriptsLst = nullptr;
        ordered_json _lists = ordered_json::array();
        ordered_json _unreadable = ordered_json::array();
        ordered_json _protos = ordered_json::array();
    };
} // namespace

std::optional<std::string> parseProtoFilter(std::string_view kind, std::string_view itemType,
    ProtoExportOptions& out) {
    out = {};
    if (kind == "item") {
        out.kind = Pro::OBJECT_TYPE::ITEM;
    } else if (kind == "critter") {
        out.kind = Pro::OBJECT_TYPE::CRITTER;
    } else if (!kind.empty()) {
        return "unknown kind '" + std::string(kind) + "' (expected item or critter)";
    }
    if (itemType.empty()) {
        return std::nullopt;
    }
    if (out.kind == Pro::OBJECT_TYPE::CRITTER) {
        return std::string("itemType filters items; it cannot be combined with kind critter");
    }
    for (std::size_t i = 0; i < kItemTypeKeys.size(); ++i) {
        if (kItemTypeKeys[i] == itemType) {
            out.kind = Pro::OBJECT_TYPE::ITEM;
            out.itemType = static_cast<Pro::ITEM_TYPE>(i);
            return std::nullopt;
        }
    }
    return "unknown itemType '" + std::string(itemType)
        + "' (expected armor, container, drug, weapon, ammo, misc or key)";
}

int exportProtos(resource::GameResources& resources, const ProtoExportOptions& options, std::ostream& out) {
    std::vector<ListSource> sources;
    if (!options.kind.has_value() || *options.kind == Pro::OBJECT_TYPE::ITEM) {
        sources.push_back({ Pro::OBJECT_TYPE::ITEM, ResourcePaths::Lst::PROTO_ITEMS });
    }
    if (!options.kind.has_value() || *options.kind == Pro::OBJECT_TYPE::CRITTER) {
        sources.push_back({ Pro::OBJECT_TYPE::CRITTER, ResourcePaths::Lst::PROTO_CRITTERS });
    }

    Exporter exporter(resources, options);
    for (const auto& source : sources) {
        if (std::string error; !exporter.walk(source, error)) {
            out << "export_protos: cannot read " << error << " (is the Fallout 2 data mounted?)\n";
            return 1;
        }
    }
    // Fallout 2 text is CP-1252; `replace` keeps dump() from throwing on stray bytes.
    out << exporter.result().dump(-1, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
