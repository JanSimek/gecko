#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QSpinBox>

#include <cstdint>

#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "support/DataTree.h"
#include "support/ProEditorData.h"
#include "support/QtFind.h"
#include "ui/widgets/ObjectPreviewWidget.h"
#include "ui/widgets/ProCommonFieldsWidget.h"
#include "ui/widgets/ProInfoPanelWidget.h"
#include "ui/widgets/ProPreviewPanelWidget.h"
#include "ui/widgets/pro/ProAmmoWidget.h"
#include "ui/widgets/pro/ProCritterWidget.h"
#include "ui/widgets/pro/ProDrugWidget.h"
#include "ui/widgets/pro/ProSceneryWidget.h"
#include "ui/widgets/pro/ProWeaponWidget.h"
#include "util/FalloutEngineEnums.h"

namespace test = geck::test;

TEST_CASE("Ammo widget uses caliber labels loaded from proto.msg", "[pro_editor]") {
    test::DataTree data("pro-editor");
    data.write("text/english/game/proto.msg", test::protoMsg());
    auto& resources = data.mount();

    geck::ProAmmoWidget widget(resources);
    auto pro = test::makeItemPro(geck::ItemType::Ammo);
    pro->ammoData.caliber = geck::fallout::enumValue(geck::fallout::CaliberType::Mm10);

    widget.loadFromPro(pro);

    auto* caliberCombo = test::findByToolTip<QComboBox>(widget, "Ammo caliber or energy cell type");
    REQUIRE(caliberCombo != nullptr);
    CHECK(caliberCombo->currentText() == "10mm");
}

TEST_CASE("Weapon widget preserves raw perk ids while using message-backed labels", "[pro_editor]") {
    test::DataTree data("pro-editor");
    data.write("text/english/game/proto.msg", test::protoMsg());
    data.write("text/english/game/perk.msg", test::perkMsg({
                                                 { geck::fallout::PerkId::WeaponLongRange, "Long Range" },
                                                 { geck::fallout::PerkId::WeaponAccurate, "Accurate" },
                                                 { geck::fallout::PerkId::WeaponPenetrate, "Penetrate" },
                                                 { geck::fallout::PerkId::WeaponKnockback, "Knockback" },
                                                 { geck::fallout::PerkId::WeaponScopeRange, "Scope Range" },
                                                 { geck::fallout::PerkId::WeaponFastReload, "Fast Reload" },
                                                 { geck::fallout::PerkId::WeaponNightSight, "Night Sight" },
                                                 { geck::fallout::PerkId::WeaponFlameboy, "Flameboy" },
                                                 { geck::fallout::PerkId::WeaponEnhancedKnockout, "Enhanced Knockout" },
                                             }));
    auto& resources = data.mount();

    geck::ProWeaponWidget widget(resources);
    auto pro = test::makeItemPro(geck::ItemType::Weapon);
    pro->weaponData.damageType = geck::fallout::enumValue(geck::fallout::DamageType::Normal);
    pro->weaponData.ammoType = geck::fallout::enumValue(geck::fallout::CaliberType::Mm10);
    pro->weaponData.perk = geck::fallout::enumValue(geck::fallout::PerkId::WeaponNightSight);

    widget.loadFromPro(pro);

    auto* perkCombo = test::findByToolTip<QComboBox>(widget, "Special perk associated with weapon");
    auto* ammoTypeCombo = test::findByToolTip<QComboBox>(widget, "Ammunition type");

    REQUIRE(perkCombo != nullptr);
    REQUIRE(ammoTypeCombo != nullptr);
    CHECK(perkCombo->currentText() == "Night Sight");
    CHECK(perkCombo->currentData().toInt() == geck::fallout::enumValue(geck::fallout::PerkId::WeaponNightSight));
    CHECK(ammoTypeCombo->currentText() == "10mm");

    const int fastReloadIndex = perkCombo->findData(geck::fallout::enumValue(geck::fallout::PerkId::WeaponFastReload));
    REQUIRE(fastReloadIndex >= 0);
    perkCombo->setCurrentIndex(fastReloadIndex);
    ammoTypeCombo->setCurrentIndex(geck::fallout::enumValue(geck::fallout::CaliberType::Gauge12));

    widget.saveToPro(pro);

    CHECK(pro->weaponData.perk == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::PerkId::WeaponFastReload)));
    CHECK(pro->weaponData.ammoType == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::CaliberType::Gauge12)));
}

TEST_CASE("Weapon energy checkbox edits sfall's extended-flags bit without clobbering the others", "[pro_editor]") {
    test::DataTree data("pro-editor");
    data.write("text/english/game/proto.msg", test::protoMsg());
    data.write("text/english/game/perk.msg", test::perkMsg({}, true));
    auto& resources = data.mount();

    using enum geck::Pro::ExtendedItemFlags;
    const auto bit = [](geck::Pro::ExtendedItemFlags flag) { return static_cast<uint32_t>(flag); };
    const uint32_t kept = bit(BIG_GUN) | bit(TWO_HANDED) | bit(ITEM_HIDDEN);

    auto pro = test::makeItemPro(geck::ItemType::Weapon);
    pro->commonItemData.flagsExt = kept | bit(ENERGY) | 0x76u; // attack modes 6 and 7

    // ProEditorDialog::saveProData order: the common fields first, then the type tab.
    geck::ProCommonFieldsWidget common(resources);
    geck::ProWeaponWidget weapon(resources);
    common.loadFromPro(pro);
    weapon.loadFromPro(pro);

    auto* energyCheck = test::findButton(weapon, "Energy weapon (sfall)");
    auto* primaryMode = test::findByToolTip<QSpinBox>(common, "Primary attack animation index (0-15)");
    REQUIRE(energyCheck != nullptr);
    REQUIRE(primaryMode != nullptr);
    CHECK(energyCheck->isChecked());

    energyCheck->setChecked(false);
    primaryMode->setValue(2);
    common.saveToPro(pro);
    weapon.saveToPro(pro);
    CHECK(pro->commonItemData.flagsExt == (kept | 0x72u));

    energyCheck->setChecked(true);
    common.saveToPro(pro);
    weapon.saveToPro(pro);
    CHECK(pro->commonItemData.flagsExt == (kept | bit(ENERGY) | 0x72u));
}

TEST_CASE("Drug widget maps stat and perk labels from message files to engine ids", "[pro_editor]") {
    test::DataTree data("pro-editor");
    data.write("text/english/game/stat.msg", test::statMsg());
    data.write("text/english/game/perk.msg",
        test::perkMsg({ { geck::fallout::PerkId::JetAddiction, "Jet Addiction" } }, true));
    auto& resources = data.mount();

    geck::ProDrugWidget widget(resources);

    SECTION("the stat and perk options come from stat.msg and perk.msg") {
        auto* statCombo = test::findByToolTip<QComboBox>(widget, "Stat 1 to modify (None=no effect)");
        auto* perkCombo = test::findByToolTip<QComboBox>(widget, "Perk applied when addicted");

        REQUIRE(statCombo != nullptr);
        REQUIRE(perkCombo != nullptr);
        REQUIRE(statCombo->count() >= 3);
        CHECK(statCombo->itemText(0) == "None");
        CHECK(statCombo->itemText(1) == "Strength");
        CHECK(statCombo->itemText(2) == "Perception");

        const int addictionIndex = perkCombo->findData(geck::fallout::enumValue(geck::fallout::PerkId::JetAddiction));
        REQUIRE(addictionIndex >= 0);
        CHECK(perkCombo->itemText(addictionIndex) == "Jet Addiction");
    }

    SECTION("stat ids round-trip through the value mapping, including None") {
        auto pro = test::makeItemPro(geck::ItemType::Drug);
        // stat0 = Perception, stat1 = "no stat" (0xFFFFFFFF sentinel), stat2 = Strength.
        pro->drugData.stat0 = static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::StatId::Perception));
        pro->drugData.stat1 = 0xFFFFFFFFu;
        pro->drugData.stat2 = static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::StatId::Strength));

        widget.loadFromPro(pro);

        auto* stat0Combo = test::findByToolTip<QComboBox>(widget, "Stat 1 to modify (None=no effect)");
        auto* stat1Combo = test::findByToolTip<QComboBox>(widget, "Stat 2 to modify (None=no effect)");
        REQUIRE(stat0Combo != nullptr);
        REQUIRE(stat1Combo != nullptr);
        // Selection is bound to the engine stat id (itemData), not the list position.
        CHECK(stat0Combo->currentData().toInt() == geck::fallout::enumValue(geck::fallout::StatId::Perception));
        CHECK(stat1Combo->currentText() == "None");

        auto out = test::makeItemPro(geck::ItemType::Drug);
        widget.saveToPro(out);

        CHECK(out->drugData.stat0 == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::StatId::Perception)));
        CHECK(out->drugData.stat1 == 0xFFFFFFFFu);
        CHECK(out->drugData.stat2 == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::StatId::Strength)));
    }
}

TEST_CASE("Scenery widget switches subtype editors and preserves subtype values", "[pro_editor]") {
    test::DataTree data("pro-editor");
    data.write("text/english/game/proto.msg", test::protoMsg());
    auto& resources = data.mount();

    geck::ProSceneryWidget widget(resources);
    auto pro = test::makeSceneryPro(geck::SceneryType::Elevator);
    pro->sceneryData.materialId = geck::fallout::enumValue(geck::fallout::MaterialType::Metal);
    pro->sceneryData.soundId = 42;
    pro->sceneryData.elevatorData.elevatorType = 7;
    pro->sceneryData.elevatorData.elevatorLevel = 3;

    widget.loadFromPro(pro);

    auto* typeCombo = test::findByToolTip<QComboBox>(widget, "Scenery subtype");
    const auto groupTitled = [&widget](const QString& title) {
        return test::findChildWhere<QGroupBox>(widget, [&title](const QGroupBox& group) { return group.title() == title; });
    };
    auto* doorGroup = groupTitled("Door Properties");
    auto* elevatorGroup = groupTitled("Elevator Properties");

    REQUIRE(typeCombo != nullptr);
    REQUIRE(doorGroup != nullptr);
    REQUIRE(elevatorGroup != nullptr);
    CHECK_FALSE(elevatorGroup->isHidden());
    CHECK(doorGroup->isHidden());

    typeCombo->setCurrentIndex(static_cast<int>(geck::SceneryType::Door));
    QApplication::processEvents();

    CHECK_FALSE(doorGroup->isHidden());
    CHECK(elevatorGroup->isHidden());

    auto checks = widget.findChildren<QCheckBox*>();
    REQUIRE(checks.size() == 1);
    checks.front()->setChecked(true);

    widget.saveToPro(pro);

    CHECK(pro->objectSubtypeId() == static_cast<unsigned>(geck::SceneryType::Door));
    CHECK(pro->sceneryData.doorData.walkThroughFlag == 1U);
}

TEST_CASE("Critter widget shows the proto fields and saves edits without dropping the rest", "[pro_editor]") {
    test::DataTree data("pro-editor");
    data.write("text/english/game/proto.msg", test::protoMsg());
    data.write("text/english/game/stat.msg", test::statMsg());
    auto& resources = data.mount();

    geck::ProCritterWidget widget(resources);
    auto pro = test::makeCritterPro();
    pro->critterData.flags = geck::Pro::setFlag(0, geck::Pro::CritterFlags::CRITTER_BARTER);
    pro->critterData.aiPacket = 12;
    pro->critterData.teamNumber = 3;
    pro->critterData.specialStats[0] = 7;
    pro->critterData.skills[0] = 55;
    pro->critterData.damageThreshold[0] = 2;
    pro->critterData.damageResist[0] = 25;
    pro->critterData.age = 30;
    pro->critterData.gender = 1;
    pro->critterData.bodyType = geck::fallout::enumValue(geck::fallout::BodyType::Robotic);

    widget.loadFromPro(pro);

    auto* aiPacketSpin = test::findByToolTip<QSpinBox>(widget, "AI packet number for critter behavior");
    auto* genderCombo = test::findByToolTip<QComboBox>(widget, "Critter gender");
    auto* bodyTypeCombo = test::findByToolTip<QComboBox>(widget, "Body type for animations");
    auto* barterCheck = test::findButton(widget, "Can Barter");
    auto* noDropCheck = test::findButton(widget, "No Drop");

    REQUIRE(aiPacketSpin != nullptr);
    REQUIRE(genderCombo != nullptr);
    REQUIRE(bodyTypeCombo != nullptr);
    REQUIRE(barterCheck != nullptr);
    REQUIRE(noDropCheck != nullptr);

    CHECK(aiPacketSpin->value() == 12);
    CHECK(barterCheck->isChecked());
    CHECK(genderCombo->currentIndex() == 1);
    CHECK(bodyTypeCombo->currentIndex() == geck::fallout::enumValue(geck::fallout::BodyType::Robotic));

    aiPacketSpin->setValue(21);
    genderCombo->setCurrentIndex(0);
    bodyTypeCombo->setCurrentIndex(geck::fallout::enumValue(geck::fallout::BodyType::Quadruped));
    noDropCheck->setChecked(true);

    widget.saveToPro(pro);

    CHECK(pro->critterData.aiPacket == 21U);
    CHECK(pro->critterData.gender == 0U);
    CHECK(pro->critterData.bodyType == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::BodyType::Quadruped)));
    CHECK(geck::Pro::hasFlag(pro->critterData.flags, geck::Pro::CritterFlags::CRITTER_BARTER));
    CHECK(geck::Pro::hasFlag(pro->critterData.flags, geck::Pro::CritterFlags::CRITTER_NO_DROP));
}

TEST_CASE("Info panel shows the proto's name, file and window title from its message and list files", "[pro_editor]") {
    test::DataTree data("pro-editor");
    data.write("text/english/game/pro_item.msg",
        test::msgLine(200, "10mm JHP")
            + test::msgLine(201, "Armor-piercing ammo for pistols"));
    data.write("proto/items/items.lst", "00000030.pro\n");
    auto& resources = data.mount();

    geck::ProInfoPanelWidget widget;
    auto pro = test::makeItemPro(geck::ItemType::Ammo);
    pro->header.PID = 0x00000001;
    pro->header.message_id = 200;

    widget.setPid(static_cast<int>(pro->header.PID));
    widget.refreshFromPro(resources, pro, static_cast<uint32_t>(widget.pid()));

    CHECK(widget.nameText() == "10mm JHP");
    CHECK(widget.filenameText() == "00000030.pro");
    CHECK(widget.windowTitleText() == "10mm JHP (Ammo) - PRO editor");
}

TEST_CASE("Preview panel uses dual item previews and a single object preview", "[pro_editor]") {
    auto itemPro = test::makeItemPro(geck::ItemType::Ammo);
    itemPro->header.FID = 123;
    itemPro->commonItemData.inventoryFID = 456;

    geck::resource::GameResources resources;
    geck::ProPreviewPanelWidget itemPreview(resources, itemPro);
    const auto itemPreviews = itemPreview.findChildren<geck::ObjectPreviewWidget*>();
    CHECK(itemPreviews.size() == 2);

    auto critterPro = test::makeCritterPro();
    critterPro->header.FID = 789;

    geck::ProPreviewPanelWidget critterPreview(resources, critterPro);
    const auto critterPreviews = critterPreview.findChildren<geck::ObjectPreviewWidget*>();
    CHECK(critterPreviews.size() == 1);
}
