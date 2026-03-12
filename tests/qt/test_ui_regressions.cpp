#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
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
#include "ui/widgets/pro/ProAmmoWidget.h"
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

QString buildPerkMsg() {
    QString contents;

    const std::array<std::pair<geck::fallout::PerkId, const char*>, geck::fallout::WEAPON_ITEM_PERKS.size()> perks = {{
        { geck::fallout::PerkId::WeaponLongRange, "Long Range" },
        { geck::fallout::PerkId::WeaponAccurate, "Accurate" },
        { geck::fallout::PerkId::WeaponPenetrate, "Penetrate" },
        { geck::fallout::PerkId::WeaponKnockback, "Knockback" },
        { geck::fallout::PerkId::WeaponScopeRange, "Scope Range" },
        { geck::fallout::PerkId::WeaponFastReload, "Fast Reload" },
        { geck::fallout::PerkId::WeaponNightSight, "Night Sight" },
        { geck::fallout::PerkId::WeaponFlameboy, "Flameboy" },
        { geck::fallout::PerkId::WeaponEnhancedKnockout, "Enhanced Knockout" },
    }};

    for (const auto& [perkId, label] : perks) {
        contents += messageLine(geck::fallout::perkNameMessageId(perkId), QString::fromLatin1(label));
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
    resources.writeGameMessageFile("text/english/game/perk.msg", buildPerkMsg());
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
