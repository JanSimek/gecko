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
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "format/ai/AiPacket.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "ui/core/EditorWidget.h"
#include "ui/core/MainWindow.h"
#include "ui/dialogs/CritterPropertiesDialog.h"
#include "ui/dialogs/InventoryViewerDialog.h"
#include "ui/dialogs/ItemSelectorDialog.h"
#include "ui/dialogs/ScriptSelectorDialog.h"
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
#include "ui/panels/ScriptsPanel.h"
#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include <QAbstractButton>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

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

TEST_CASE("ScriptSelectorDialog shows index/filename/name/comment columns and returns the program index", "[qt][script]") {
    const std::vector<geck::ScriptSelectorDialog::Entry> entries = {
        { 0, "obj_dude.int", "The Chosen One", "Player script." },
        { 1, "obj_door.int", "", "A door" },
        { 2, "raiders.int", "Raiders", "" },
    };
    geck::ScriptSelectorDialog dialog(entries, /*currentIndex=*/2);
    auto* table = dialog.findChild<QTableWidget*>();
    REQUIRE(table != nullptr);
    REQUIRE(table->rowCount() == 3);
    REQUIRE(table->columnCount() == 4);

    // Find a row by its filename so the assertions don't depend on the (sortable) row order. Columns are
    // index / filename / name / comment.
    int dudeRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 1)->text() == "obj_dude.int") {
            dudeRow = row;
            break;
        }
    }
    REQUIRE(dudeRow >= 0);
    CHECK(table->item(dudeRow, 0)->data(Qt::DisplayRole).toInt() == 0);
    CHECK(table->item(dudeRow, 2)->text() == "The Chosen One");
    CHECK(table->item(dudeRow, 3)->text() == "Player script."); // scripts.lst comment column

    // currentIndex == 2 is preselected; selectedIndex returns the program index, not the row.
    CHECK(dialog.selectedIndex() == 2);
    table->selectRow(dudeRow);
    CHECK(dialog.selectedIndex() == 0);
}

TEST_CASE("EditorWidget::canSaveInPlace accepts only real on-disk files, not VFS paths", "[qt][save]") {
    // A map opened from the game data carries a vfspp path that looks absolute but is not a writable
    // filesystem location — saving it in place would try to create "/maps" at the root (the reported bug).
    CHECK_FALSE(geck::EditorWidget::canSaveInPlace("/maps/arbridge.map"));
    // A brand-new / unsaved map has no real file yet.
    CHECK_FALSE(geck::EditorWidget::canSaveInPlace("arbridge.map"));
    CHECK_FALSE(geck::EditorWidget::canSaveInPlace(std::filesystem::path{}));

    // A real, existing file can be saved straight back.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path real = std::filesystem::path(dir.path().toStdString()) / "saved.map";
    {
        std::ofstream out(real);
        out << "map";
    }
    CHECK(geck::EditorWidget::canSaveInPlace(real));
}

TEST_CASE("CritterPropertiesDialog names the AI packet from ai.txt and falls back to the raw number", "[qt][critter]") {
    geck::AiTxt aiTxt;
    aiTxt.add(geck::AiPacket{ .packetNum = 5, .name = "Coward" });
    aiTxt.add(geck::AiPacket{ .packetNum = 21, .name = "Berserker" });

    SECTION("current packet is in ai.txt -> shown by name, value round-trips") {
        geck::CritterPropertiesDialog dialog(21, 1, 100, 0, 0, aiTxt);
        auto* combo = dialog.findChild<QComboBox*>();
        REQUIRE(combo != nullptr);
        CHECK(combo->count() == 2); // the two packets, no raw fallback entry
        CHECK(combo->currentText().contains("Berserker"));
        CHECK(dialog.getAiPacket() == 21u);
    }

    SECTION("current packet not in ai.txt -> a raw fallback entry preserves the value") {
        geck::CritterPropertiesDialog dialog(99, 1, 100, 0, 0, aiTxt);
        auto* combo = dialog.findChild<QComboBox*>();
        REQUIRE(combo != nullptr);
        CHECK(combo->count() == 3); // two packets + a raw "Packet 99" entry
        CHECK(combo->currentText().contains("99"));
        CHECK(dialog.getAiPacket() == 99u);
    }

    SECTION("an arbitrary number can be typed into the editable combo") {
        geck::CritterPropertiesDialog dialog(5, 1, 100, 0, 0, aiTxt);
        auto* combo = dialog.findChild<QComboBox*>();
        REQUIRE(combo != nullptr);
        REQUIRE(combo->isEditable());
        combo->setCurrentText("150"); // not a named packet -> a freely-typed raw number
        CHECK(dialog.getAiPacket() == 150u);
    }
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
    const Expect expected[] = {
        { "aaa.pro", geck::Pro::makePid(geck::Pro::OBJECT_TYPE::ITEM, 1) },
        { "bbb.pro", geck::Pro::makePid(geck::Pro::OBJECT_TYPE::ITEM, 2) },
        { "ccc.pro", geck::Pro::makePid(geck::Pro::OBJECT_TYPE::ITEM, 3) },
    };
    for (int i = 0; i < 3; ++i) {
        QTreeWidgetItem* row = tree->topLevelItem(i); // sorted by name -> aaa, bbb, ccc
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

TEST_CASE("MapInfoPanel names the map's own script from scripts.lst and scrname.msg", "[qt][mapinfo]") {
    ResourceDataScope data;
    data.writeGameMessageFile("scripts/scripts.lst", "obj_dude.int    ; player\nzclrat.int      ; rat\n");
    // scriptDisplayName(programIndex 0) reads scrname.msg[0 + 101] = 101.
    data.writeGameMessageFile("text/english/game/scrname.msg", messageLine(101, QStringLiteral("The Chosen One")));
    data.mount();

    auto settings = std::make_shared<geck::Settings>();
    auto map = makeMap("testmap.map");
    map->getMapFile().header.script_id = 1; // 1-based -> scripts.lst index 0 (obj_dude.int)

    geck::MapInfoPanel panel(data.resources(), settings);
    panel.setMap(map.get());

    auto* scriptEdit = panel.findChild<QLineEdit*>("mapScript");
    REQUIRE(scriptEdit != nullptr);
    // .lst filename plus the scrname.msg description, resolved even with no .gam file mounted.
    CHECK(scriptEdit->text() == QStringLiteral("obj_dude.int — The Chosen One"));
}

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

TEST_CASE("ScriptsPanel lists the map's scripts with resolved filename and name", "[qt][scripts]") {
    ResourceDataScope data;
    data.writeGameMessageFile("scripts/scripts.lst", "obj_dude.int    ; player\nzclrat.int      ; rat\n");
    // scriptDisplayName(programIndex 0) reads scrname.msg[0 + 101] = 101.
    data.writeGameMessageFile("text/english/game/scrname.msg", messageLine(101, QStringLiteral("The Chosen One")));
    data.mount();

    auto map = makeMap("testmap.map");

    // One CRITTER-section script (program index 0 -> obj_dude.int) owned by object 42, plus a second one
    // so the row count reflects every section's scripts.
    auto& mapFile = map->getMapFile();
    mapFile.map_scripts[static_cast<int>(geck::MapScript::ScriptType::CRITTER)].push_back(
        geck::MapScript::makeObjectScript(geck::MapScript::ScriptType::CRITTER, 0, 0, 42));
    mapFile.map_scripts[static_cast<int>(geck::MapScript::ScriptType::ITEM)].push_back(
        geck::MapScript::makeObjectScript(geck::MapScript::ScriptType::ITEM, 1, 1, 7));

    // The map's own (header) script is 1-based; script_id 1 resolves to scripts.lst index 0
    // (obj_dude.int) and is listed as a separate "Map" row.
    mapFile.header.script_id = 1;

    geck::ScriptsPanel panel(data.resources());
    panel.setMap(map.get());

    auto* table = panel.findChild<QTableWidget*>("scriptsTable"); // the script list (not the local-vars table)
    REQUIRE(table != nullptr);
    CHECK(table->rowCount() == 3); // 2 section scripts + the map's own header script

    // Find the row whose Script ID cell is 0 (sorting may reorder rows) and check its filename + name.
    bool foundScriptZero = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* idItem = table->item(row, 1); // COL_SCRIPT_ID
        REQUIRE(idItem != nullptr);
        if (idItem->data(Qt::DisplayRole).toInt() == 0) {
            foundScriptZero = true;
            CHECK(table->item(row, 2)->text() == QStringLiteral("obj_dude.int"));   // COL_FILENAME
            CHECK(table->item(row, 3)->text() == QStringLiteral("The Chosen One")); // COL_NAME
        }
    }
    CHECK(foundScriptZero);

    // The map's own script appears as a distinct "Map"-section row resolving to obj_dude.int /
    // "The Chosen One" (script_id 1 -> scripts.lst index 0).
    bool foundMapRow = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* sectionItem = table->item(row, 0); // COL_SECTION
        REQUIRE(sectionItem != nullptr);
        if (sectionItem->text() == QStringLiteral("Map")) {
            foundMapRow = true;
            CHECK(table->item(row, 1)->data(Qt::DisplayRole).toInt() == 0);         // COL_SCRIPT_ID (0-based)
            CHECK(table->item(row, 2)->text() == QStringLiteral("obj_dude.int"));   // COL_FILENAME
            CHECK(table->item(row, 3)->text() == QStringLiteral("The Chosen One")); // COL_NAME
        }
    }
    CHECK(foundMapRow);

    // Double-clicking an object-owned row emits scriptObjectActivated carrying that row's SID (stored in
    // the COL_SCRIPT_ID UserRole); double-clicking the ownerless "Map" row emits nothing.
    QSignalSpy activatedSpy(&panel, &geck::ScriptsPanel::scriptObjectActivated);
    int critterRow = -1;
    int mapRow = -1;
    qulonglong critterSid = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString section = table->item(row, 0)->text();  // COL_SECTION
        const QTableWidgetItem* idItem = table->item(row, 1); // COL_SCRIPT_ID
        if (section == QStringLiteral("Critter")) {
            critterRow = row;
            critterSid = idItem->data(Qt::UserRole).toULongLong();
        } else if (section == QStringLiteral("Map")) {
            mapRow = row;
        }
    }
    REQUIRE(critterRow >= 0);
    REQUIRE(mapRow >= 0);
    CHECK(critterSid != static_cast<qulonglong>(geck::MapScript::NONE)); // owned row carries a real SID

    Q_EMIT table->cellDoubleClicked(critterRow, 1);
    REQUIRE(activatedSpy.count() == 1);
    CHECK(activatedSpy.takeFirst().at(0).toInt() == static_cast<int>(critterSid));

    Q_EMIT table->cellDoubleClicked(mapRow, 0); // ownerless map row -> no navigation
    CHECK(activatedSpy.count() == 0);

    // Regression: an active filter is re-applied after the table is re-populated (e.g. a map switch),
    // rather than silently showing every row again.
    auto* filter = panel.findChild<QLineEdit*>();
    REQUIRE(filter != nullptr);
    filter->setText("obj_dude"); // matches the critter section row and the Map header-script row
    panel.setMap(map.get());     // re-populate; the filter must still be honoured
    int visibleRows = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->isRowHidden(row)) {
            ++visibleRows;
        }
    }
    CHECK(visibleRows == 2); // both the section script and the map's own script resolve to obj_dude.int
    filter->clear();

    // Clearing the map empties the table without crashing.
    panel.setMap(nullptr);
    CHECK(table->rowCount() == 0);
}

TEST_CASE("ScriptsPanel shows the selected script's local variables", "[qt][scripts]") {
    ResourceDataScope data;
    data.writeGameMessageFile("scripts/scripts.lst", "obj_dude.int    ; player\n");
    data.mount();

    auto map = makeMap("testmap.map");
    auto& mapFile = map->getMapFile();

    // A critter script whose two local variables sit at offset 0 in the map's flat LVAR array.
    auto script = geck::MapScript::makeObjectScript(geck::MapScript::ScriptType::CRITTER, 0, 0, 42);
    script.local_var_offset = 0;
    script.local_var_count = 2;
    mapFile.map_scripts[static_cast<int>(geck::MapScript::ScriptType::CRITTER)].push_back(script);
    mapFile.map_local_vars = { 111, 222 };

    geck::ScriptsPanel panel(data.resources());
    panel.setMap(map.get());

    auto* scriptsTable = panel.findChild<QTableWidget*>("scriptsTable");
    auto* lvarTable = panel.findChild<QTableWidget*>("localVarsTable");
    REQUIRE(scriptsTable != nullptr);
    REQUIRE(lvarTable != nullptr);
    REQUIRE(scriptsTable->rowCount() == 1);
    CHECK(lvarTable->rowCount() == 0); // nothing selected yet

    scriptsTable->selectRow(0); // selecting the script reveals its local variables
    REQUIRE(lvarTable->rowCount() == 2);
    CHECK(lvarTable->item(0, 1)->data(Qt::DisplayRole).toInt() == 111); // column 1 = Value
    CHECK(lvarTable->item(1, 1)->data(Qt::DisplayRole).toInt() == 222);
}

// The unified Exit-Grids toolbar tool replaces the two former buttons with one checkable button
// plus a dropdown whose two items ("Draw region" / "Place single hex") are exclusive: triggering
// one ticks it, unticks the other, and keeps the tool button checked. (Driving the editor mode
// itself needs game data to construct the editor, so that half is covered separately below against
// EditorWidget directly.)
TEST_CASE("Unified Exit-Grids tool exposes one button with an exclusive sub-mode dropdown", "[qt][exitgrids]") {
    removeTestSettings();

    auto resources = std::make_shared<geck::resource::GameResources>();
    geck::MainWindow window(resources, std::make_shared<geck::Settings>());

    // The two former separate buttons are gone, replaced by one "Exit Grids" button.
    CHECK(findAction(window, "Mark Exits") == nullptr);
    CHECK(findAction(window, "Place Exit Grids") == nullptr);

    auto* exitGridsAction = findAction(window, "Exit Grids");
    auto* drawRegion = findAction(window, "Draw region");
    auto* placeHex = findAction(window, "Place single hex");
    REQUIRE(exitGridsAction != nullptr);
    REQUIRE(drawRegion != nullptr);
    REQUIRE(placeHex != nullptr);
    CHECK(exitGridsAction->isCheckable());
    CHECK(exitGridsAction->menu() != nullptr); // dropdown attached to the button

    // Default sub-mode is "Draw region".
    CHECK(drawRegion->isChecked());
    CHECK_FALSE(placeHex->isChecked());

    // Triggering "Place single hex" makes it the (exclusive) checked sub-mode and keeps the tool on.
    placeHex->trigger();
    QApplication::processEvents();
    CHECK(placeHex->isChecked());
    CHECK_FALSE(drawRegion->isChecked());
    CHECK(exitGridsAction->isChecked());

    // Triggering "Draw region" flips the exclusive selection back.
    drawRegion->trigger();
    QApplication::processEvents();
    CHECK(drawRegion->isChecked());
    CHECK_FALSE(placeHex->isChecked());
    CHECK(exitGridsAction->isChecked());
}

// The editor-mode half: EditorWidget::currentMode() moves between PlaceExitGrid and MarkExits via
// the same setters the unified tool ultimately drives, pinning that the two sub-modes map to
// distinct editor modes. Constructing an EditorWidget pulls in the HexRenderer, which eagerly loads
// art/misc/HEX.frm, so this can only run where the game data is mounted; without it we skip rather
// than fail (the structural dropdown test above covers the wiring with no data).
TEST_CASE("EditorWidget switches between the two exit-grid sub-modes", "[qt][exitgrids]") {
    auto resources = std::make_shared<geck::resource::GameResources>();
    std::unique_ptr<geck::EditorWidget> editor;
    try {
        editor = std::make_unique<geck::EditorWidget>(*resources, makeMap("exitgrids.map"));
    } catch (const std::exception& e) {
        SKIP(std::string("EditorWidget needs mounted game data (HEX.frm): ") + e.what());
    }

    CHECK(editor->currentMode() == geck::EditorMode::Select);

    editor->setMarkExitsMode(true); // "Draw region"
    CHECK(editor->currentMode() == geck::EditorMode::MarkExits);

    editor->setExitGridPlacementMode(true); // "Place single hex"
    CHECK(editor->currentMode() == geck::EditorMode::PlaceExitGrid);

    editor->setMode(geck::EditorMode::Select);
    CHECK(editor->currentMode() == geck::EditorMode::Select);
}

TEST_CASE("MapInfoPanel edits a global variable value and persists it to the map's .gam", "[qt][mapinfo]") {
    ResourceDataScope data;
    data.writeGameMessageFile("scripts/scripts.lst", "obj_dude.int    ; player\n");
    // The map's global variables come from its .gam (basename = map filename without extension). The
    // values displayed AND edited are the .gam's MAP_GLOBAL_VARS values, since a BASE map's globals are
    // re-read from the .gam by the engine, ignoring the .map's blocks.
    data.writeGameMessageFile("maps/testmap.gam",
        "MAP_GLOBAL_VARS:\nMVAR_first := 10;\nMVAR_second := 20;\n");
    data.mount();

    // A writable folder, mounted last so its edited .gam copy shadows the source.
    QTemporaryDir writableDir;
    REQUIRE(writableDir.isValid());
    const std::filesystem::path writableRoot = writableDir.path().toStdString();
    data.resources().files().addDataPath(writableRoot.string());

    auto settings = std::make_shared<geck::Settings>();
    settings->setDataPaths({ writableRoot }); // the writable folder IS the visible Data Path edits go to

    auto map = makeMap("testmap.map");
    auto& mapFile = map->getMapFile();
    // A real base map's stored block mirrors its .gam (the engine snapshots it on save); start aligned.
    mapFile.map_global_vars = { 10, 20 };
    mapFile.header.num_global_vars = 2;

    geck::MapInfoPanel panel(data.resources(), settings);
    panel.setMap(map.get());

    auto* tree = panel.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);
    // Two variable rows; the count/source summary is a label under the tree, not a row in it.
    REQUIRE(tree->topLevelItemCount() == 2);

    QTreeWidgetItem* firstVar = tree->topLevelItem(0);
    REQUIRE(firstVar != nullptr);
    REQUIRE(firstVar->data(1, Qt::UserRole).isValid());
    CHECK(firstVar->data(1, Qt::UserRole).toInt() == 0);
    REQUIRE((firstVar->flags() & Qt::ItemIsEditable) != 0);
    CHECK(firstVar->text(1) == QStringLiteral("10")); // value shown is the .gam's MAP_GLOBAL_VARS value

    // The count/source summary is a label under the tree, not a row in it.
    auto* summaryLabel = panel.findChild<QLabel*>("globalVarsSummary");
    REQUIRE(summaryLabel != nullptr);
    CHECK(summaryLabel->text() == QStringLiteral("Total: 2 variables from testmap.gam"));

    QSignalSpy varSpy(&panel, &geck::MapInfoPanel::mapVariablesChanged);

    // Editing the first variable's Value cell emits the signal; nothing is written until persist.
    firstVar->setText(1, QStringLiteral("-42")); // negatives accepted (signed int32)
    CHECK(varSpy.count() == 1);

    // The edit is mirrored into the .map's stored block too, so the .map and .gam stay in sync.
    REQUIRE(mapFile.map_global_vars.size() == 2);
    CHECK(mapFile.map_global_vars[0] == -42);
    CHECK(mapFile.map_global_vars[1] == 20); // the other variable is untouched

    // Persisting writes the .gam to the writable Data Path, with the edited value and the other variable
    // untouched. A re-read reflects the edit.
    panel.persistMapVars();
    const std::filesystem::path savedGam = writableRoot / "maps" / "testmap.gam";
    REQUIRE(std::filesystem::exists(savedGam));
    const std::string saved = geck::io::readFile(savedGam);
    CHECK(saved.find("MVAR_first := -42;") != std::string::npos);
    CHECK(saved.find("MVAR_second := 20;") != std::string::npos); // the other variable is untouched
}

namespace {

// Shared scaffolding for the add/remove global-variable tests: mount a map's .gam, configure a writable
// Data Path, and hand back the bits each test needs. Keeps the new tests under the duplication budget.
struct GlobalVarFixture {
    ResourceDataScope data;
    QTemporaryDir writableDir;
    std::shared_ptr<geck::Settings> settings;
    std::unique_ptr<geck::Map> map;

    explicit GlobalVarFixture(const char* gamContents) {
        data.writeGameMessageFile("scripts/scripts.lst", "obj_dude.int    ; player\n");
        data.writeGameMessageFile("maps/testmap.gam", gamContents);
        data.mount();

        REQUIRE(writableDir.isValid());
        const std::filesystem::path writableRoot = writableDir.path().toStdString();
        data.resources().files().addDataPath(writableRoot.string());

        settings = std::make_shared<geck::Settings>();
        settings->setDataPaths({ writableRoot });

        map = makeMap("testmap.map");
    }
};

// Click the given button role on the next modal dialog Qt shows. Posted before triggering the action so
// the (otherwise blocking) confirmation dialog is answered without a human.
void answerNextDialog(QMessageBox::StandardButton button) {
    QTimer::singleShot(0, [button]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                if (QAbstractButton* target = box->button(button)) {
                    target->click();
                    return;
                }
            }
        }
    });
}

} // namespace

TEST_CASE("MapInfoPanel adds a map global variable via the name field and Add button", "[qt][mapinfo]") {
    GlobalVarFixture fixture("MAP_GLOBAL_VARS:\nMVAR_first := 10;\n");
    fixture.map->getMapFile().map_global_vars = { 10 }; // .map block aligned with the .gam
    fixture.map->getMapFile().header.num_global_vars = 1;

    geck::MapInfoPanel panel(fixture.data.resources(), fixture.settings);
    panel.setMap(fixture.map.get());

    auto* nameEdit = panel.findChild<QLineEdit*>("newGlobalVarName");
    auto* addButton = panel.findChild<QPushButton*>("addGlobalVar");
    auto* tree = panel.findChild<QTreeWidget*>();
    REQUIRE(nameEdit != nullptr);
    REQUIRE(addButton != nullptr);
    REQUIRE(tree != nullptr);

    // One variable row before adding (the summary is a label, not a row).
    REQUIRE(tree->topLevelItemCount() == 1);

    QSignalSpy varSpy(&panel, &geck::MapInfoPanel::mapVariablesChanged);

    nameEdit->setText("MVAR_new");
    addButton->click();

    // The new (editable, value 0) row appears appended last, and the map is modified.
    REQUIRE(tree->topLevelItemCount() == 2);
    QTreeWidgetItem* added = tree->topLevelItem(1);
    REQUIRE(added != nullptr);
    CHECK(added->text(0) == QStringLiteral("MVAR_new"));
    CHECK(added->text(1) == QStringLiteral("0"));
    REQUIRE(added->data(1, Qt::UserRole).isValid());
    CHECK(added->data(1, Qt::UserRole).toInt() == 1); // appended last -> index 1
    CHECK((added->flags() & Qt::ItemIsEditable) != 0);
    CHECK(varSpy.count() == 1);
    CHECK(nameEdit->text().isEmpty()); // the field is cleared after a successful add

    // The .map's stored block grew in lockstep with the .gam.
    auto& addedMapFile = fixture.map->getMapFile();
    REQUIRE(addedMapFile.map_global_vars.size() == 2);
    CHECK(addedMapFile.map_global_vars[1] == 0);
    CHECK(addedMapFile.header.num_global_vars == 2);

    // Persisting writes the appended variable after the existing one.
    panel.persistMapVars();
    const std::filesystem::path savedGam = std::filesystem::path(fixture.writableDir.path().toStdString()) / "maps" / "testmap.gam";
    REQUIRE(std::filesystem::exists(savedGam));
    const std::string out = geck::io::readFile(savedGam);
    CHECK(out.find("MVAR_first := 10;") != std::string::npos);
    CHECK(out.find("MVAR_new := 0;") != std::string::npos);
    CHECK(out.find("MVAR_first") < out.find("MVAR_new"));
}

TEST_CASE("MapInfoPanel removes the last map global variable after confirmation", "[qt][mapinfo]") {
    GlobalVarFixture fixture("MAP_GLOBAL_VARS:\nMVAR_first := 10;\nMVAR_last := 99;\n");
    fixture.map->getMapFile().map_global_vars = { 10, 99 }; // .map block aligned with the .gam
    fixture.map->getMapFile().header.num_global_vars = 2;

    geck::MapInfoPanel panel(fixture.data.resources(), fixture.settings);
    panel.setMap(fixture.map.get());

    auto* tree = panel.findChild<QTreeWidget*>();
    auto* removeButton = panel.findChild<QPushButton*>("removeGlobalVar");
    REQUIRE(tree != nullptr);
    REQUIRE(removeButton != nullptr);
    REQUIRE(tree->topLevelItemCount() == 2); // two variables (the summary is a label)

    // Selecting the last variable row enables Remove.
    QTreeWidgetItem* lastVar = tree->topLevelItem(1);
    REQUIRE(lastVar != nullptr);
    REQUIRE(lastVar->data(1, Qt::UserRole).toInt() == 1);
    tree->setCurrentItem(lastVar);
    CHECK(removeButton->isEnabled());

    QSignalSpy varSpy(&panel, &geck::MapInfoPanel::mapVariablesChanged);

    // Confirm the warning -> the row is dropped, the survivor remains, and the map is modified.
    answerNextDialog(QMessageBox::Yes);
    removeButton->click();

    REQUIRE(tree->topLevelItemCount() == 1); // one variable (the summary is a label)
    QTreeWidgetItem* survivor = tree->topLevelItem(0);
    REQUIRE(survivor != nullptr);
    CHECK(survivor->text(0) == QStringLiteral("MVAR_first"));
    CHECK(varSpy.count() == 1);

    // The .map's stored block shrank in lockstep with the .gam.
    auto& removedMapFile = fixture.map->getMapFile();
    REQUIRE(removedMapFile.map_global_vars.size() == 1);
    CHECK(removedMapFile.map_global_vars[0] == 10);
    CHECK(removedMapFile.header.num_global_vars == 1);

    // The removed variable is gone from the persisted .gam.
    panel.persistMapVars();
    const std::filesystem::path savedGam = std::filesystem::path(fixture.writableDir.path().toStdString()) / "maps" / "testmap.gam";
    REQUIRE(std::filesystem::exists(savedGam));
    const std::string out = geck::io::readFile(savedGam);
    CHECK(out.find("MVAR_first := 10;") != std::string::npos);
    CHECK(out.find("MVAR_last") == std::string::npos);
}
