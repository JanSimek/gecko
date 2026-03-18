#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTreeWidget>

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "ui/core/MainWindow.h"
#include "ui/dialogs/InventoryViewerDialog.h"
#include "ui/widgets/ObjectPreviewWidget.h"
#include "ui/widgets/ProInfoPanelWidget.h"
#include "ui/widgets/ProPreviewPanelWidget.h"
#include "ui/widgets/pro/ProAmmoWidget.h"
#include "ui/widgets/pro/ProCritterWidget.h"
#include "ui/widgets/pro/ProDrugWidget.h"
#include "ui/widgets/pro/ProSceneryWidget.h"
#include "ui/widgets/pro/ProWeaponWidget.h"
#include "resource/GameResources.h"
#include "util/FalloutEngineEnums.h"

namespace {

QString messageLine(int id, const QString& text) {
    return QString("{%1}{}{%2}\n").arg(id).arg(text);
}

void writeTextFile(const QString& path, const QString& contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        throw std::runtime_error(("Failed to open " + path).toStdString());
    }

    QTextStream stream(&file);
    stream << contents;
}

class ResourceDataScope {
public:
    ResourceDataScope() {
        if (!_root.isValid()) {
            throw std::runtime_error("Failed to create temporary data directory");
        }
    }

    ~ResourceDataScope() {
        _resources->clearAllDataPaths();
    }

    void mount() {
        _resources->clearAllDataPaths();
        _resources->files().addDataPath(_root.path().toStdString());
    }

    void writeGameMessageFile(const QString& relativePath, const QString& contents) {
        const QString fullPath = _root.filePath(relativePath);
        QFileInfo fileInfo(fullPath);
        QDir().mkpath(fileInfo.path());
        writeTextFile(fullPath, contents);
    }

    geck::resource::GameResources& resources() {
        return *_resources;
    }

    std::shared_ptr<geck::resource::GameResources> sharedResources() {
        return _resources;
    }

private:
    QTemporaryDir _root;
    std::shared_ptr<geck::resource::GameResources> _resources = std::make_shared<geck::resource::GameResources>();
};

QString buildProtoMsg() {
    QString contents;

    const std::array<const char*, geck::fallout::enumCount<geck::fallout::MaterialType>()> materials = {
        "Glass",
        "Metal",
        "Plastic",
        "Wood",
        "Dirt",
        "Stone",
        "Cement",
        "Leather",
    };

    for (size_t index = 0; index < materials.size(); ++index) {
        const auto material = static_cast<geck::fallout::MaterialType>(index);
        contents += messageLine(geck::fallout::protoMessageId(material), QString::fromLatin1(materials[index]));
    }

    const std::array<const char*, geck::fallout::enumCount<geck::fallout::SceneryType>()> sceneryTypes = {
        "Door",
        "Stairs",
        "Elevator",
        "Ladder Bottom",
        "Ladder Top",
        "Generic",
    };

    for (size_t index = 0; index < sceneryTypes.size(); ++index) {
        const auto sceneryType = static_cast<geck::fallout::SceneryType>(index);
        contents += messageLine(geck::fallout::protoMessageId(sceneryType), QString::fromLatin1(sceneryTypes[index]));
    }

    const std::array<const char*, geck::fallout::enumCount<geck::fallout::BodyType>()> bodyTypes = {
        "Biped",
        "Quadruped",
        "Robotic",
    };

    for (size_t index = 0; index < bodyTypes.size(); ++index) {
        const auto bodyType = static_cast<geck::fallout::BodyType>(index);
        contents += messageLine(geck::fallout::protoMessageId(bodyType), QString::fromLatin1(bodyTypes[index]));
    }

    const std::array<const char*, geck::fallout::enumCount<geck::fallout::DamageType>()> damageTypes = {
        "Normal",
        "Laser",
        "Fire",
        "Plasma",
        "Electrical",
        "EMP",
        "Explosion",
    };

    for (size_t index = 0; index < damageTypes.size(); ++index) {
        const auto damageType = static_cast<geck::fallout::DamageType>(index);
        contents += messageLine(geck::fallout::protoMessageId(damageType), QString::fromLatin1(damageTypes[index]));
    }

    const std::array<const char*, geck::fallout::enumCount<geck::fallout::CaliberType>()> calibers = {
        "None",
        "Rocket",
        "Flamethrower Fuel",
        "C Energy Cell",
        "D Energy Cell",
        ".223",
        "5mm",
        ".40 cal",
        "10mm",
        ".44 cal",
        "14mm",
        "12-gauge",
        "9mm",
        "BB",
        ".45 cal",
        "2mm",
        "4.7mm caseless",
        "HN needler",
        "7.62mm",
    };

    for (size_t index = 0; index < calibers.size(); ++index) {
        const auto caliber = static_cast<geck::fallout::CaliberType>(index);
        contents += messageLine(geck::fallout::protoMessageId(caliber), QString::fromLatin1(calibers[index]));
    }

    return contents;
}

QString buildPerkMsg(std::initializer_list<std::pair<geck::fallout::PerkId, const char*>> namedPerks = {},
    bool includeAllPerks = false) {
    QString contents;

    if (includeAllPerks) {
        for (int index = 0; index < geck::fallout::enumValue(geck::fallout::PerkId::Count); ++index) {
            auto perkId = static_cast<geck::fallout::PerkId>(index);
            contents += messageLine(geck::fallout::perkNameMessageId(perkId), QString("Perk %1").arg(index));
        }
    }

    for (const auto& [perkId, label] : namedPerks) {
        contents += messageLine(geck::fallout::perkNameMessageId(perkId), QString::fromLatin1(label));
    }

    return contents;
}

QString buildStatMsg() {
    QString contents;
    for (int index = 0; index < geck::fallout::enumValue(geck::fallout::StatId::Count); ++index) {
        auto statId = static_cast<geck::fallout::StatId>(index);
        QString label = QString("Stat %1").arg(index);

        switch (statId) {
            case geck::fallout::StatId::Strength:
                label = "Strength";
                break;
            case geck::fallout::StatId::Perception:
                label = "Perception";
                break;
            case geck::fallout::StatId::RadiationResistance:
                label = "Radiation Resistance";
                break;
            case geck::fallout::StatId::PoisonResistance:
                label = "Poison Resistance";
                break;
            default:
                break;
        }

        contents += messageLine(geck::fallout::statNameMessageId(statId), label);
    }

    return contents;
}

QString normalizeActionText(QString text) {
    text.remove('&');
    return text;
}

QAction* findAction(QObject& root, const QString& text) {
    const QString wanted = text;
    const auto actions = root.findChildren<QAction*>();
    for (QAction* action : actions) {
        if (normalizeActionText(action->text()) == wanted) {
            return action;
        }
    }

    return nullptr;
}

QComboBox* findComboBoxByToolTip(QWidget& root, const QString& tooltip) {
    const QString wanted = tooltip;
    const auto combos = root.findChildren<QComboBox*>();
    for (QComboBox* combo : combos) {
        if (combo->toolTip() == wanted) {
            return combo;
        }
    }

    return nullptr;
}

QSpinBox* findSpinBoxByToolTip(QWidget& root, const QString& tooltip) {
    const QString wanted = tooltip;
    const auto spinBoxes = root.findChildren<QSpinBox*>();
    for (QSpinBox* spinBox : spinBoxes) {
        if (spinBox->toolTip() == wanted) {
            return spinBox;
        }
    }

    return nullptr;
}

QCheckBox* findCheckBoxByText(QWidget& root, const QString& text) {
    const QString wanted = text;
    const auto checkBoxes = root.findChildren<QCheckBox*>();
    for (QCheckBox* checkBox : checkBoxes) {
        if (checkBox->text() == wanted) {
            return checkBox;
        }
    }

    return nullptr;
}

QLabel* findCenteredLabelByText(QWidget& root, const QString& text) {
    const QString wanted = text;
    const auto labels = root.findChildren<QLabel*>();
    for (QLabel* label : labels) {
        if (label->text() == wanted && label->alignment().testFlag(Qt::AlignHCenter) && label->alignment().testFlag(Qt::AlignVCenter)) {
            return label;
        }
    }

    return nullptr;
}

QGroupBox* findGroupBoxByTitle(QWidget& root, const QString& title) {
    const auto groups = root.findChildren<QGroupBox*>();
    for (QGroupBox* group : groups) {
        if (group->title() == title) {
            return group;
        }
    }

    return nullptr;
}

void removeTestSettings() {
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir().mkpath(configRoot);
    QDir geckoDir(configRoot + "/gecko");
    if (geckoDir.exists()) {
        geckoDir.removeRecursively();
    }
    QDir().mkpath(configRoot + "/gecko");
}

std::shared_ptr<geck::Pro> makeItemPro(geck::Pro::ITEM_TYPE itemType) {
    auto pro = std::make_shared<geck::Pro>(std::filesystem::path("test.pro"));
    pro->header.PID = 0;
    pro->setObjectSubtypeId(static_cast<unsigned>(itemType));
    return pro;
}

std::shared_ptr<geck::Pro> makeSceneryPro(geck::Pro::SCENERY_TYPE sceneryType) {
    auto pro = std::make_shared<geck::Pro>(std::filesystem::path("test.pro"));
    pro->header.PID = static_cast<int32_t>(geck::Pro::OBJECT_TYPE::SCENERY) << 24;
    pro->setObjectSubtypeId(static_cast<unsigned>(sceneryType));
    return pro;
}

std::shared_ptr<geck::Pro> makeCritterPro() {
    auto pro = std::make_shared<geck::Pro>(std::filesystem::path("test.pro"));
    pro->header.PID = static_cast<int32_t>(geck::Pro::OBJECT_TYPE::CRITTER) << 24;
    return pro;
}

} // namespace

TEST_CASE("Inventory viewer shows empty inventory state instead of an empty table", "[qt][inventory]") {
    auto mapObject = std::make_shared<geck::MapObject>();
    mapObject->objects_in_inventory = 0;

    geck::resource::GameResources resources;
    geck::InventoryViewerDialog dialog(resources, mapObject);

    auto* stack = dialog.findChild<QStackedWidget*>();
    auto* emptyLabel = findCenteredLabelByText(dialog, "No inventory items");
    auto* tree = dialog.findChild<QTreeWidget*>();

    REQUIRE(stack != nullptr);
    REQUIRE(emptyLabel != nullptr);
    REQUIRE(tree != nullptr);
    REQUIRE(stack->currentWidget() == emptyLabel);
    REQUIRE(tree->topLevelItemCount() == 0);
}

TEST_CASE("Ammo widget uses caliber labels loaded from proto.msg", "[qt][pro]") {
    ResourceDataScope resources;
    resources.writeGameMessageFile("text/english/game/proto.msg", buildProtoMsg());
    resources.mount();

    geck::ProAmmoWidget widget(resources.resources());
    auto pro = makeItemPro(geck::Pro::ITEM_TYPE::AMMO);
    pro->ammoData.caliber = geck::fallout::enumValue(geck::fallout::CaliberType::Mm10);

    widget.loadFromPro(pro);

    auto* caliberCombo = widget.findChild<QComboBox*>();
    REQUIRE(caliberCombo != nullptr);
    REQUIRE(caliberCombo->currentText() == "10mm");
}

TEST_CASE("Weapon widget preserves raw perk ids while using message-backed labels", "[qt][pro]") {
    ResourceDataScope resources;
    resources.writeGameMessageFile("text/english/game/proto.msg", buildProtoMsg());
    resources.writeGameMessageFile("text/english/game/perk.msg", buildPerkMsg({
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
    resources.mount();

    geck::ProWeaponWidget widget(resources.resources());
    auto pro = makeItemPro(geck::Pro::ITEM_TYPE::WEAPON);
    pro->weaponData.damageType = geck::fallout::enumValue(geck::fallout::DamageType::Normal);
    pro->weaponData.ammoType = geck::fallout::enumValue(geck::fallout::CaliberType::Mm10);
    pro->weaponData.perk = geck::fallout::enumValue(geck::fallout::PerkId::WeaponNightSight);

    widget.loadFromPro(pro);

    auto* perkCombo = findComboBoxByToolTip(widget, "Special perk associated with weapon");
    auto* ammoTypeCombo = findComboBoxByToolTip(widget, "Ammunition type");

    REQUIRE(perkCombo != nullptr);
    REQUIRE(ammoTypeCombo != nullptr);
    REQUIRE(perkCombo->currentText() == "Night Sight");
    REQUIRE(perkCombo->currentData().toInt() == geck::fallout::enumValue(geck::fallout::PerkId::WeaponNightSight));
    REQUIRE(ammoTypeCombo->currentText() == "10mm");

    const int fastReloadIndex = perkCombo->findData(geck::fallout::enumValue(geck::fallout::PerkId::WeaponFastReload));
    REQUIRE(fastReloadIndex >= 0);
    perkCombo->setCurrentIndex(fastReloadIndex);
    ammoTypeCombo->setCurrentIndex(geck::fallout::enumValue(geck::fallout::CaliberType::Gauge12));

    widget.saveToPro(pro);

    REQUIRE(pro->weaponData.perk == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::PerkId::WeaponFastReload)));
    REQUIRE(pro->weaponData.ammoType == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::CaliberType::Gauge12)));
}

TEST_CASE("Drug widget loads stat and perk options from message files", "[qt][pro]") {
    ResourceDataScope resources;
    resources.writeGameMessageFile("text/english/game/stat.msg", buildStatMsg());
    resources.writeGameMessageFile("text/english/game/perk.msg",
        buildPerkMsg({ { geck::fallout::PerkId::JetAddiction, "Jet Addiction" } }, true));
    resources.mount();

    geck::ProDrugWidget widget(resources.resources());
    auto* statCombo = findComboBoxByToolTip(widget, "Stat 1 to modify (None=no effect)");
    auto* perkCombo = findComboBoxByToolTip(widget, "Perk applied when addicted");

    REQUIRE(statCombo != nullptr);
    REQUIRE(perkCombo != nullptr);
    REQUIRE(statCombo->count() >= 3);
    REQUIRE(statCombo->itemText(0) == "None");
    REQUIRE(statCombo->itemText(1) == "Strength");
    REQUIRE(statCombo->itemText(2) == "Perception");

    const int addictionIndex = perkCombo->findData(geck::fallout::enumValue(geck::fallout::PerkId::JetAddiction));
    REQUIRE(addictionIndex >= 0);
    REQUIRE(perkCombo->itemText(addictionIndex) == "Jet Addiction");
}

TEST_CASE("Scenery widget switches subtype editors and preserves subtype values", "[qt][pro]") {
    ResourceDataScope resources;
    resources.writeGameMessageFile("text/english/game/proto.msg", buildProtoMsg());
    resources.mount();

    geck::ProSceneryWidget widget(resources.resources());
    auto pro = makeSceneryPro(geck::Pro::SCENERY_TYPE::ELEVATOR);
    pro->sceneryData.materialId = geck::fallout::enumValue(geck::fallout::MaterialType::Metal);
    pro->sceneryData.soundId = 42;
    pro->sceneryData.elevatorData.elevatorType = 7;
    pro->sceneryData.elevatorData.elevatorLevel = 3;

    widget.loadFromPro(pro);

    auto* typeCombo = findComboBoxByToolTip(widget, "Scenery subtype");
    auto* doorGroup = findGroupBoxByTitle(widget, "Door Properties");
    auto* elevatorGroup = findGroupBoxByTitle(widget, "Elevator Properties");

    REQUIRE(typeCombo != nullptr);
    REQUIRE(doorGroup != nullptr);
    REQUIRE(elevatorGroup != nullptr);
    REQUIRE_FALSE(elevatorGroup->isHidden());
    REQUIRE(doorGroup->isHidden());

    typeCombo->setCurrentIndex(static_cast<int>(geck::Pro::SCENERY_TYPE::DOOR));
    QApplication::processEvents();

    REQUIRE_FALSE(doorGroup->isHidden());
    REQUIRE(elevatorGroup->isHidden());

    auto checks = widget.findChildren<QCheckBox*>();
    REQUIRE(checks.size() == 1);
    checks.front()->setChecked(true);

    widget.saveToPro(pro);

    REQUIRE(pro->objectSubtypeId() == static_cast<unsigned>(geck::Pro::SCENERY_TYPE::DOOR));
    REQUIRE(pro->sceneryData.doorData.walkThroughFlag == 1U);
}

TEST_CASE("Critter widget preserves live critter fields after extraction", "[qt][pro]") {
    ResourceDataScope resources;
    resources.writeGameMessageFile("text/english/game/proto.msg", buildProtoMsg());
    resources.writeGameMessageFile("text/english/game/stat.msg", buildStatMsg());
    resources.mount();

    geck::ProCritterWidget widget(resources.resources());
    auto pro = makeCritterPro();
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

    auto* aiPacketSpin = findSpinBoxByToolTip(widget, "AI packet number for critter behavior");
    auto* genderCombo = findComboBoxByToolTip(widget, "Critter gender");
    auto* bodyTypeCombo = findComboBoxByToolTip(widget, "Body type for animations");
    auto* barterCheck = findCheckBoxByText(widget, "Can Barter");
    auto* noDropCheck = findCheckBoxByText(widget, "No Drop");

    REQUIRE(aiPacketSpin != nullptr);
    REQUIRE(genderCombo != nullptr);
    REQUIRE(bodyTypeCombo != nullptr);
    REQUIRE(barterCheck != nullptr);
    REQUIRE(noDropCheck != nullptr);

    REQUIRE(aiPacketSpin->value() == 12);
    REQUIRE(barterCheck->isChecked());
    REQUIRE(genderCombo->currentIndex() == 1);
    REQUIRE(bodyTypeCombo->currentIndex() == geck::fallout::enumValue(geck::fallout::BodyType::Robotic));

    aiPacketSpin->setValue(21);
    genderCombo->setCurrentIndex(0);
    bodyTypeCombo->setCurrentIndex(geck::fallout::enumValue(geck::fallout::BodyType::Quadruped));
    noDropCheck->setChecked(true);

    widget.saveToPro(pro);

    REQUIRE(pro->critterData.aiPacket == 21U);
    REQUIRE(pro->critterData.gender == 0U);
    REQUIRE(pro->critterData.bodyType == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::BodyType::Quadruped)));
    REQUIRE(geck::Pro::hasFlag(pro->critterData.flags, geck::Pro::CritterFlags::CRITTER_BARTER));
    REQUIRE(geck::Pro::hasFlag(pro->critterData.flags, geck::Pro::CritterFlags::CRITTER_NO_DROP));
}

TEST_CASE("Info panel derives display state from PRO data", "[qt][pro]") {
    ResourceDataScope resources;
    resources.writeGameMessageFile("text/english/game/pro_item.msg",
        messageLine(200, "10mm JHP")
            + messageLine(201, "Armor-piercing ammo for pistols"));
    resources.writeGameMessageFile("proto/items/items.lst", "00000030.pro\n");
    resources.mount();

    geck::ProInfoPanelWidget widget;
    auto pro = makeItemPro(geck::Pro::ITEM_TYPE::AMMO);
    pro->header.PID = 0x00000001;
    pro->header.message_id = 200;

    widget.setPid(static_cast<int>(pro->header.PID));
    widget.refreshFromPro(resources.resources(), pro, static_cast<uint32_t>(widget.pid()));

    REQUIRE(widget.nameText() == "10mm JHP");
    REQUIRE(widget.filenameText() == "00000030.pro");
    REQUIRE(widget.windowTitleText() == "10mm JHP (Ammo) - PRO editor");
}

TEST_CASE("Preview panel uses dual item previews and a single object preview", "[qt][pro]") {
    auto itemPro = makeItemPro(geck::Pro::ITEM_TYPE::AMMO);
    itemPro->header.FID = 123;
    itemPro->commonItemData.inventoryFID = 456;

    geck::resource::GameResources resources;
    geck::ProPreviewPanelWidget itemPreview(resources, itemPro);
    const auto itemPreviews = itemPreview.findChildren<geck::ObjectPreviewWidget*>();
    REQUIRE(itemPreviews.size() == 2);

    auto critterPro = makeCritterPro();
    critterPro->header.FID = 789;

    geck::ProPreviewPanelWidget critterPreview(resources, critterPro);
    const auto critterPreviews = critterPreview.findChildren<geck::ObjectPreviewWidget*>();
    REQUIRE(critterPreviews.size() == 1);
}

TEST_CASE("MainWindow panel toggles stay wired in the no-map layout", "[qt][mainwindow]") {
    removeTestSettings();

    auto resources = std::make_shared<geck::resource::GameResources>();
    geck::MainWindow window(resources);
    window.show();
    QTest::qWait(250);

    auto* mapInfoDock = window.findChild<QDockWidget*>("MapInfoDock");
    auto* selectionDock = window.findChild<QDockWidget*>("SelectionDock");
    auto* tilePaletteDock = window.findChild<QDockWidget*>("TilePaletteDock");
    auto* objectPaletteDock = window.findChild<QDockWidget*>("ObjectPaletteDock");
    auto* fileBrowserDock = window.findChild<QDockWidget*>("FileBrowserDock");
    auto* selectionAction = findAction(window, "Selection");

    REQUIRE(mapInfoDock != nullptr);
    REQUIRE(selectionDock != nullptr);
    REQUIRE(tilePaletteDock != nullptr);
    REQUIRE(objectPaletteDock != nullptr);
    REQUIRE(fileBrowserDock != nullptr);
    REQUIRE(selectionAction != nullptr);

    REQUIRE_FALSE(mapInfoDock->isVisible());
    REQUIRE_FALSE(selectionDock->isVisible());
    REQUIRE_FALSE(tilePaletteDock->isVisible());
    REQUIRE_FALSE(objectPaletteDock->isVisible());
    REQUIRE(fileBrowserDock->isVisible());
    REQUIRE_FALSE(selectionAction->isChecked());

    selectionAction->trigger();
    QApplication::processEvents();

    REQUIRE(selectionDock->isVisible());
    REQUIRE(selectionAction->isChecked());

    selectionAction->trigger();
    QApplication::processEvents();

    REQUIRE_FALSE(selectionDock->isVisible());
    REQUIRE_FALSE(selectionAction->isChecked());
}
