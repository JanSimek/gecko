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
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTableWidget>
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
#include "ui/dialogs/ItemSelectorDialog.h"
#include "ui/widgets/DataPathsWidget.h"
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
#include "util/FileIo.h"
#include "ui/Settings.h"
#include "ui/panels/MapInfoPanel.h"
#include "format/map/Map.h"
#include <QLineEdit>
#include <QPushButton>

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

TEST_CASE("Drug widget round-trips stat ids through value mapping, including None", "[qt][pro]") {
    ResourceDataScope resources;
    resources.writeGameMessageFile("text/english/game/stat.msg", buildStatMsg());
    resources.writeGameMessageFile("text/english/game/perk.msg",
        buildPerkMsg({ { geck::fallout::PerkId::JetAddiction, "Jet Addiction" } }, true));
    resources.mount();

    geck::ProDrugWidget widget(resources.resources());

    auto pro = makeItemPro(geck::Pro::ITEM_TYPE::DRUG);
    // stat0 = Perception, stat1 = "no stat" (0xFFFFFFFF sentinel), stat2 = Strength.
    pro->drugData.stat0 = static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::StatId::Perception));
    pro->drugData.stat1 = 0xFFFFFFFFu;
    pro->drugData.stat2 = static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::StatId::Strength));

    widget.loadFromPro(pro);

    auto* stat0Combo = findComboBoxByToolTip(widget, "Stat 1 to modify (None=no effect)");
    auto* stat1Combo = findComboBoxByToolTip(widget, "Stat 2 to modify (None=no effect)");
    REQUIRE(stat0Combo != nullptr);
    REQUIRE(stat1Combo != nullptr);
    // Selection is bound to the engine stat id (itemData), not the list position.
    REQUIRE(stat0Combo->currentData().toInt() == geck::fallout::enumValue(geck::fallout::StatId::Perception));
    REQUIRE(stat1Combo->currentText() == "None");

    auto out = makeItemPro(geck::Pro::ITEM_TYPE::DRUG);
    widget.saveToPro(out);

    REQUIRE(out->drugData.stat0 == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::StatId::Perception)));
    REQUIRE(out->drugData.stat1 == 0xFFFFFFFFu);
    REQUIRE(out->drugData.stat2 == static_cast<uint32_t>(geck::fallout::enumValue(geck::fallout::StatId::Strength)));
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

TEST_CASE("ItemSelectorDialog lists items.lst entries by their item PID", "[qt][inventory]") {
    ResourceDataScope resources;
    resources.writeGameMessageFile("proto/items/items.lst", "aaa.pro\nbbb.pro\nccc.pro\n");
    resources.mount();

    geck::ItemSelectorDialog dialog(resources.resources());
    auto* tree = dialog.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);
    REQUIRE(tree->topLevelItemCount() == 3);

    // No .pro files are mounted, so describeItem cannot resolve a name; the dialog falls back to the
    // .pro filename and shows the item PID = makePid(ITEM, 1-based items.lst line) in the PID column.
    struct Expect {
        const char* name;
        uint32_t pid;
    };
    const std::array<Expect, 3> expected = { {
        { "aaa.pro", geck::Pro::makePid(geck::Pro::OBJECT_TYPE::ITEM, 1) },
        { "bbb.pro", geck::Pro::makePid(geck::Pro::OBJECT_TYPE::ITEM, 2) },
        { "ccc.pro", geck::Pro::makePid(geck::Pro::OBJECT_TYPE::ITEM, 3) },
    } };
    for (int i = 0; i < 3; ++i) {
        const QTreeWidgetItem* row = tree->topLevelItem(i); // sorted by name -> aaa, bbb, ccc
        const QString expectedPid = QString("0x%1").arg(expected[i].pid, 8, 16, QChar('0'));
        CHECK(row->text(0).toStdString() == expected[i].name);          // Name column (filename fallback)
        CHECK(row->text(2).toStdString() == expectedPid.toStdString()); // PID column (visible)
    }

    // Nothing chosen yet; the amount defaults to 1.
    CHECK_FALSE(dialog.selectedPid().has_value());
    CHECK(dialog.selectedAmount() == 1);

    // Selecting a row exposes that row's PID through the public API.
    tree->setCurrentItem(tree->topLevelItem(0));
    REQUIRE(dialog.selectedPid().has_value());
    CHECK(dialog.selectedPid().value() == expected[0].pid);
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
    geck::MainWindow window(resources, std::make_shared<geck::Settings>());
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

TEST_CASE("DataPathsWidget shows highest priority on top and preserves stored order", "[qt][datapaths]") {
    auto settings = std::make_shared<geck::Settings>();
    geck::DataPathsWidget widget(settings);

    // Stored order is lowest-priority-first: the loader mounts in this order and the last-mounted
    // source wins. (.dat paths normalise to themselves, so the round-trip is exact.)
    const std::vector<std::filesystem::path> stored{ "/data/base.dat", "/data/patch.dat", "/data/mymod.dat" };
    widget.setDataPaths(stored);

    // Round-trip: the stored order is preserved exactly, so existing configs load identically.
    CHECK(widget.getDataPaths() == stored);

    // The table displays it reversed, highest priority first: the stored-last source is the top row.
    auto* table = widget.findChild<QTableWidget*>();
    REQUIRE(table != nullptr);
    REQUIRE(table->rowCount() == 3);
    CHECK(table->item(0, 1)->text().toStdString() == "/data/mymod.dat"); // top row = highest priority
    CHECK(table->item(2, 1)->text().toStdString() == "/data/base.dat");  // bottom row = lowest
    CHECK(table->item(0, 0)->text().startsWith("1"));                    // priority counts from 1 at the top
}

#ifdef GECK_SCRIPTING_ENABLED
#include "ui/panels/ScriptConsoleWidget.h"
#include <QPlainTextEdit>
#include <QPushButton>

TEST_CASE("Script console setSource loads the editor and feeds Run", "[qt][scripting]") {
    geck::ScriptConsoleWidget console;

    // setSource is what "Execute script" in the file browser uses to drop a .luau into the console.
    const QString src = "api:paintFloor(0, 271)";
    console.setSource(src);

    QString runSource;
    QObject::connect(&console, &geck::ScriptConsoleWidget::runRequested,
        [&runSource](const QString& s) { runSource = s; });

    // Pressing Run hands back exactly what setSource put in the editor.
    const auto buttons = console.findChildren<QPushButton*>();
    REQUIRE_FALSE(buttons.isEmpty());
    buttons.first()->click();
    CHECK(runSource == src);
}
#endif

// Phase A of the map-info editor work: the Map Info panel resolves the current map's friendly names
// (maps.txt lookup_name + the per-elevation map.msg display name) read-only, reusing MapNameResolver.
namespace {

// A map loaded from `mapName`, with an in-memory MapFile. The header's 16-byte filename field is
// upper-cased + NUL-padded like real .map files (e.g. "ARTEMPLE.MAP\0\0\0\0"), which does NOT match
// maps.txt — so the panel must resolve by the file basename (map->filename()), not header.filename.
std::unique_ptr<geck::Map> makeMap(const std::string& mapName, int elevation = 0) {
    auto map = std::make_unique<geck::Map>(mapName);
    auto mapFile = std::make_unique<geck::Map::MapFile>(geck::Map::createEmptyMapFile());
    QString upper = QString::fromStdString(mapName).toUpper();
    mapFile->header.filename = upper.toStdString() + std::string(4, '\0'); // mismatched on purpose
    mapFile->header.player_default_elevation = static_cast<uint32_t>(elevation);
    map->setMapFile(std::move(mapFile));
    return map;
}

} // namespace

TEST_CASE("MapInfoPanel shows resolved map.msg display name and maps.txt lookup name", "[qt][mapinfo]") {
    ResourceDataScope data;
    data.writeGameMessageFile("data/maps.txt", "[Map 0]\nlookup_name=Test Town\nmap_name=testmap\n");
    // displayName(index 0, elevation 0) reads map.msg[0*3 + 0 + 200] = 200.
    data.writeGameMessageFile("text/english/game/map.msg", messageLine(200, QStringLiteral("Test Display")));
    data.mount();

    auto settings = std::make_shared<geck::Settings>();
    auto map = makeMap("testmap.map");

    geck::MapInfoPanel panel(data.resources(), settings);
    panel.setMap(map.get());

    auto* displayEdit = panel.findChild<QLineEdit*>("mapDisplayName");
    auto* lookupEdit = panel.findChild<QLineEdit*>("mapLookupName");
    REQUIRE(displayEdit != nullptr);
    REQUIRE(lookupEdit != nullptr);
    CHECK(displayEdit->text() == QStringLiteral("Test Display"));
    CHECK(lookupEdit->text() == QStringLiteral("Test Town"));

    // A map not listed in maps.txt resolves to a placeholder (read-only), not a crash.
    auto unknown = makeMap("nosuch.map");
    panel.setMap(unknown.get());
    CHECK(lookupEdit->text() == QStringLiteral("(not in maps.txt)"));
    CHECK(lookupEdit->isReadOnly());
}

TEST_CASE("MapInfoPanel persists edited map names to a writable Data Path and reflects them", "[qt][mapinfo]") {
    ResourceDataScope data;
    data.writeGameMessageFile("data/maps.txt", "[Map 0]\nlookup_name=Test Town\nmap_name=testmap\n");
    data.writeGameMessageFile("text/english/game/map.msg", messageLine(200, QStringLiteral("Test Display")));
    data.mount();

    // A writable folder, mounted after the source so its edited copy shadows the source.
    QTemporaryDir writableDir;
    REQUIRE(writableDir.isValid());
    const std::filesystem::path writableRoot = writableDir.path().toStdString();
    data.resources().files().addDataPath(writableRoot.string());

    auto settings = std::make_shared<geck::Settings>();
    settings->setDataPaths({ writableRoot }); // the writable folder IS the visible Data Path edits go to

    auto map = makeMap("testmap.map");
    geck::MapInfoPanel panel(data.resources(), settings);
    panel.setMap(map.get());

    auto* lookupEdit = panel.findChild<QLineEdit*>("mapLookupName");
    auto* displayEdit = panel.findChild<QLineEdit*>("mapDisplayName");
    auto* hint = panel.findChild<QLabel*>("mapNamesOverlayHint");
    REQUIRE(lookupEdit != nullptr);
    REQUIRE(displayEdit != nullptr);
    REQUIRE(hint != nullptr);
    REQUIRE(lookupEdit->text() == QStringLiteral("Test Town"));
    CHECK(hint->isHidden()); // there is a writable Data Path -> no "add a folder" hint

    // Editing a name emits mapNamesChanged — the signal the main window turns into "map modified".
    QSignalSpy modifiedSpy(&panel, &geck::MapInfoPanel::mapNamesChanged);
    QTest::keyClicks(lookupEdit, "Z"); // user input fires textEdited -> mapNamesChanged
    CHECK(modifiedSpy.count() >= 1);

    // Set the final values and persist — what the main window calls after writing the .map.
    lookupEdit->setText("Renamed Town");
    lookupEdit->setModified(true);
    displayEdit->setText("New Display");
    displayEdit->setModified(true);
    panel.persistMapNames();

    // Persisted: the writable Data Path carries both edits.
    const std::filesystem::path savedMaps = writableRoot / "data" / "maps.txt";
    const std::filesystem::path savedMsg = writableRoot / "text" / "english" / "game" / "map.msg";
    REQUIRE(std::filesystem::exists(savedMaps));
    REQUIRE(std::filesystem::exists(savedMsg));
    CHECK(geck::io::readFile(savedMaps).find("lookup_name=Renamed Town") != std::string::npos);
    CHECK(geck::io::readFile(savedMsg).find("{200}{}{New Display}") != std::string::npos);

    // Reflected: after the re-mount + resolver rebuild, the panel shows both new names.
    CHECK(lookupEdit->text() == QStringLiteral("Renamed Town"));
    CHECK(displayEdit->text() == QStringLiteral("New Display"));
    CHECK(hint->isHidden()); // still a writable Data Path -> still no hint
}

TEST_CASE("MapInfoPanel hints to add a writable Data Path when none is configured", "[qt][mapinfo]") {
    ResourceDataScope data;
    data.writeGameMessageFile("data/maps.txt", "[Map 0]\nlookup_name=Test Town\nmap_name=testmap\n");
    data.writeGameMessageFile("text/english/game/map.msg", messageLine(200, QStringLiteral("Test Display")));
    data.mount();

    auto settings = std::make_shared<geck::Settings>(); // no writable folder in the Data Paths

    auto map = makeMap("testmap.map");
    geck::MapInfoPanel panel(data.resources(), settings);
    panel.setMap(map.get());

    auto* lookupEdit = panel.findChild<QLineEdit*>("mapLookupName");
    auto* hint = panel.findChild<QLabel*>("mapNamesOverlayHint");
    REQUIRE(lookupEdit != nullptr);
    REQUIRE(hint != nullptr);
    REQUIRE(lookupEdit->text() == QStringLiteral("Test Town")); // registered -> the name fields are editable

    // With no writable folder in Data Paths, the panel hints the user to add one instead of saving
    // anywhere hidden.
    CHECK_FALSE(hint->isHidden());
    CHECK(hint->text().contains("writable folder"));
}
