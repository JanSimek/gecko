#include "GameEnums.h"

#include "../util/AmmoCaliberNames.h"
#include "../util/FalloutEngineEnums.h"
#include "../util/MessageEnumUtils.h"
#include "../util/ProHelper.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace geck::game::enums {
namespace {

QStringList toQStringList(const std::vector<std::string>& values) {
    QStringList items;
    items.reserve(static_cast<qsizetype>(values.size()));

    for (const std::string& value : values) {
        items.append(QString::fromStdString(value));
    }

    return items;
}

std::vector<std::string> requireMessageRange(const Msg* msg, int firstId, size_t count, const char* label) {
    if (auto values = loadMessageRange(msg, firstId, count)) {
        return *values;
    }

    throw std::runtime_error(QString("Missing or incomplete %1 labels").arg(label).toStdString());
}

QVector<EnumOption> toEnumOptions(const std::vector<MessageEnumOption>& options) {
    QVector<EnumOption> values;
    values.reserve(static_cast<qsizetype>(options.size()));

    for (const MessageEnumOption& option : options) {
        values.append({ option.value, QString::fromStdString(option.label) });
    }

    return values;
}

QVector<EnumOption> prependNoPerkOption(const std::vector<MessageEnumOption>& options) {
    QVector<EnumOption> values;
    values.reserve(static_cast<qsizetype>(options.size() + 1));
    values.append({ fallout::NO_ITEM_PERK, QString() });

    for (const MessageEnumOption& option : options) {
        values.append({ option.value, QString::fromStdString(option.label) });
    }

    return values;
}

std::vector<MessageEnumOption> requireMessageOptions(const Msg* msg, std::span<const MessageEnumSpec> specs, const char* label) {
    if (auto options = loadMessageOptions(msg, specs)) {
        return *options;
    }

    throw std::runtime_error(QString("Missing or incomplete %1 labels").arg(label).toStdString());
}

std::vector<std::string> requireAmmoCaliberNames(const Msg* protoMsg) {
    if (auto names = ammoCaliberNamesFromProtoMsg(protoMsg)) {
        return *names;
    }

    throw std::runtime_error("Missing or incomplete proto.msg caliber labels");
}

template <typename Enum>
QStringList requireProtoEnumNames(Enum firstValue, const char* label) {
    return toQStringList(requireMessageRange(
        ProHelper::protoMsgFile(),
        fallout::protoMessageId(firstValue),
        fallout::enumCount<Enum>(),
        label));
}

template <size_t N>
std::vector<MessageEnumOption> requirePerkOptions(const std::array<fallout::PerkId, N>& perks, const char* label) {
    std::vector<MessageEnumSpec> specs;
    specs.reserve(perks.size());

    for (fallout::PerkId perk : perks) {
        specs.push_back({ fallout::enumValue(perk), fallout::perkNameMessageId(perk) });
    }

    return requireMessageOptions(ProHelper::perkMsgFile(), specs, label);
}

std::vector<MessageEnumOption> requireAllPerkOptions() {
    std::vector<MessageEnumSpec> specs;
    specs.reserve(fallout::enumCount<fallout::PerkId>());

    for (int perkValue = 0; perkValue < fallout::enumValue(fallout::PerkId::Count); ++perkValue) {
        auto perk = static_cast<fallout::PerkId>(perkValue);
        specs.push_back({ perkValue, fallout::perkNameMessageId(perk) });
    }

    return requireMessageOptions(ProHelper::perkMsgFile(), specs, "perk.msg perk");
}

} // namespace

QStringList damageTypes7() {
    return requireProtoEnumNames(fallout::DamageType::Normal, "proto.msg damage type");
}

QStringList damageTypes9() {
    QStringList values = damageTypes7();
    values.append(toQStringList(requireMessageRange(
        ProHelper::statMsgFile(),
        fallout::statNameMessageId(fallout::StatId::RadiationResistance),
        2,
        "stat.msg critter resistance")));
    return values;
}

QStringList statNames() {
    return toQStringList(requireMessageRange(
        ProHelper::statMsgFile(),
        fallout::statNameMessageId(fallout::StatId::Strength),
        fallout::enumCount<fallout::StatId>(),
        "stat.msg stat"));
}

QStringList materialTypes() {
    return requireProtoEnumNames(fallout::MaterialType::Glass, "proto.msg material");
}

QStringList ammoCaliberTypes() {
    return toQStringList(requireAmmoCaliberNames(ProHelper::protoMsgFile()));
}

QVector<EnumOption> allPerkOptions() {
    return toEnumOptions(requireAllPerkOptions());
}

QVector<EnumOption> weaponPerkOptions() {
    return prependNoPerkOption(requirePerkOptions(fallout::WEAPON_ITEM_PERKS, "perk.msg weapon perk"));
}

QVector<EnumOption> armorPerkOptions() {
    return prependNoPerkOption(requirePerkOptions(fallout::ARMOR_ITEM_PERKS, "perk.msg armor perk"));
}

QStringList critterBodyTypes() {
    return requireProtoEnumNames(fallout::BodyType::Biped, "proto.msg body type");
}

QStringList sceneryTypes() {
    return requireProtoEnumNames(fallout::SceneryType::Door, "proto.msg scenery");
}

} // namespace geck::game::enums
