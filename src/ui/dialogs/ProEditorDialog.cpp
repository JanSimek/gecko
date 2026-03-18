#include "ProEditorDialog.h"
#include "MessageSelectorDialog.h"
#include "../UIConstants.h"
#include "../widgets/pro/ProAmmoWidget.h"
#include "../widgets/pro/ProArmorWidget.h"
#include "../widgets/pro/ProContainerKeyWidget.h"
#include "../widgets/pro/ProCritterWidget.h"
#include "../widgets/pro/ProDrugWidget.h"
#include "../widgets/pro/ProMiscItemWidget.h"
#include "../widgets/pro/ProSceneryWidget.h"
#include "../widgets/pro/ProTabWidget.h"
#include "../widgets/pro/ProTileWidget.h"
#include "../widgets/pro/ProWallWidget.h"
#include "../widgets/pro/ProWeaponWidget.h"

#include <QMessageBox>
#include <QFileDialog>
#include <filesystem>
#include <algorithm>
#include <spdlog/spdlog.h>

#include "../../writer/pro/ProWriter.h"
#include "../../resource/GameResources.h"
#include "FrmSelectorDialog.h"

#include <util/ProHelper.h>

namespace geck {

ProEditorDialog::ProEditorDialog(resource::GameResources& resources, std::shared_ptr<Pro> pro, QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _contentLayout(nullptr)
    , _tabWidget(nullptr)
    , _buttonBox(nullptr)
    , _infoPanelWidget(nullptr)
    , _previewPanelWidget(nullptr)
    , _commonTab(nullptr)
    , _miscTab(nullptr)
    , _commonFieldsWidget(nullptr)
    , _typeSpecificWidget(nullptr)
    , _pro(pro)
    , _resources(resources) {

    setWindowTitle("PRO Editor");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    resize(ui::constants::dialog_sizes::PRO_EDITOR_WIDTH, ui::constants::dialog_sizes::PRO_EDITOR_HEIGHT);

    setupUI();

    loadProData();

    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    adjustSize();
}

void ProEditorDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);

    // Create horizontal layout: Left Info Panel | Right Type-Specific Fields
    _contentLayout = new QHBoxLayout();

    // === LEFT PANEL: Image + Name + Description + Common Fields ===
    _infoPanelWidget = new ProInfoPanelWidget(this);
    _infoPanelWidget->setFixedWidth(ui::constants::sizes::WIDTH_INFO_PANEL);
    _previewPanelWidget = new ProPreviewPanelWidget(_resources, _pro, this);
    _infoPanelWidget->setPreviewWidget(_previewPanelWidget);
    connect(_infoPanelWidget, &ProInfoPanelWidget::editMessageRequested, this, &ProEditorDialog::onEditMessageClicked);
    connect(_infoPanelWidget, &ProInfoPanelWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);
    connect(_previewPanelWidget, &ProPreviewPanelWidget::inventoryFidSelected, this, [this](int32_t fid) {
        _pro->commonItemData.inventoryFID = fid;
        refreshInfoPanel();
    });
    connect(_previewPanelWidget, &ProPreviewPanelWidget::groundFidSelected, this, [this](int32_t fid) {
        _pro->header.FID = fid;
        refreshInfoPanel();
    });
    connect(_previewPanelWidget, &ProPreviewPanelWidget::objectFidSelected, this, [this](int32_t fid) {
        _pro->header.FID = fid;
        refreshInfoPanel();
    });

    // === RIGHT PANEL: Tabbed Interface ===
    _tabWidget = new QTabWidget(this);
    _tabWidget->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);

    // Add main panels to content layout
    _contentLayout->addWidget(_infoPanelWidget, 0); // Fixed width
    _contentLayout->addWidget(_tabWidget, 1);       // Flexible width

    // Setup tabbed content
    setupTabs();

    // Button box
    _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(_buttonBox, &QDialogButtonBox::accepted, this, &ProEditorDialog::onAccept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    _mainLayout->addLayout(_contentLayout);

    _mainLayout->addWidget(_buttonBox);
}

void ProEditorDialog::setupTabs() {
    setupCommonTab();
    setupTypeSpecificTabs();
}

void ProEditorDialog::setupCommonTab() {
    _commonTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(_commonTab);
    layout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    layout->setSpacing(ui::constants::SPACING_FORM);

    _commonFieldsWidget = new ProCommonFieldsWidget(_resources, this);
    layout->addWidget(_commonFieldsWidget);

    connect(_commonFieldsWidget, &ProCommonFieldsWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);

    _tabWidget->addTab(_commonTab, "Common");
}

void ProEditorDialog::setupTypeSpecificTabs() {
    if (!_pro)
        return;

    if (_pro->type() == Pro::OBJECT_TYPE::MISC) {
        setupMiscTab();
        return;
    }

    _typeSpecificWidget = createTypeSpecificWidget();
    if (_typeSpecificWidget) {
        registerTypeSpecificWidget(_typeSpecificWidget);
        _tabWidget->addTab(_typeSpecificWidget, _typeSpecificWidget->getTabLabel());
    }
}

void ProEditorDialog::setupMiscTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::MISC)
        return;

    _miscTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(_miscTab);
    layout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    layout->setSpacing(ui::constants::SPACING_FORM);
    layout->addStretch();

    _tabWidget->addTab(_miscTab, "Misc");
}

ProTabWidget* ProEditorDialog::createTypeSpecificWidget() {
    if (!_pro) {
        return nullptr;
    }

    switch (_pro->type()) {
        case Pro::OBJECT_TYPE::ITEM:
            switch (_pro->itemType()) {
                case Pro::ITEM_TYPE::ARMOR:
                    return new ProArmorWidget(_resources);
                case Pro::ITEM_TYPE::CONTAINER:
                case Pro::ITEM_TYPE::KEY:
                    return new ProContainerKeyWidget(_resources);
                case Pro::ITEM_TYPE::DRUG:
                    return new ProDrugWidget(_resources);
                case Pro::ITEM_TYPE::WEAPON:
                    return new ProWeaponWidget(_resources);
                case Pro::ITEM_TYPE::AMMO:
                    return new ProAmmoWidget(_resources);
                case Pro::ITEM_TYPE::MISC:
                    return new ProMiscItemWidget(_resources);
                default:
                    break;
            }
            break;
        case Pro::OBJECT_TYPE::CRITTER:
            return new ProCritterWidget(_resources);
        case Pro::OBJECT_TYPE::SCENERY:
            return new ProSceneryWidget(_resources);
        case Pro::OBJECT_TYPE::WALL:
            return new ProWallWidget(_resources);
        case Pro::OBJECT_TYPE::TILE:
            return new ProTileWidget(_resources);
        case Pro::OBJECT_TYPE::MISC:
            break;
        default:
            break;
    }

    return nullptr;
}

void ProEditorDialog::registerTypeSpecificWidget(ProTabWidget* widget) {
    if (!widget) {
        return;
    }

    connect(widget, &ProTabWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);
    connect(widget, &ProTabWidget::fidLabelSelectorRequested, this,
        [this](QLabel* targetLabel, int32_t* fidStorage, Frm::FRM_TYPE objectType) {
            openFrmSelectorForLabel(targetLabel, fidStorage, objectType);
        });
}

void ProEditorDialog::loadProData() {
    try {
        if (_infoPanelWidget) {
            _infoPanelWidget->setPid(_pro->header.PID);
        }

        if (_commonFieldsWidget) {
            _commonFieldsWidget->loadFromPro(_pro);
            bool isItem = (_pro->type() == Pro::OBJECT_TYPE::ITEM);
            _commonFieldsWidget->setItemFieldsVisible(isItem);
        }

    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::loadProData() - exception loading common data: {}", e.what());
        throw;
    }

    if (_typeSpecificWidget) {
        _typeSpecificWidget->loadFromPro(_pro);
    }

    refreshInfoPanel();

    if (_previewPanelWidget) {
        _previewPanelWidget->refresh();
    }
}

void ProEditorDialog::saveProData() {
    if (_infoPanelWidget) {
        _pro->header.PID = _infoPanelWidget->pid();
    }

    if (_commonFieldsWidget) {
        _commonFieldsWidget->saveToPro(_pro);
    }

    if (_typeSpecificWidget) {
        _typeSpecificWidget->saveToPro(_pro);
    }
}

void ProEditorDialog::onAccept() {
    saveProData();

    QString suggestedName = QString::fromStdString(_pro->path().filename().string());
    QString filePath = QFileDialog::getSaveFileName(
        this,
        "Save PRO File",
        suggestedName,
        "PRO Files (*.pro);;All Files (*)");

    if (filePath.isEmpty()) {
        return; // User cancelled
    }

    std::filesystem::path savePath(filePath.toStdString());
    if (std::filesystem::exists(savePath)) {
        std::filesystem::path backupPath = savePath;
        backupPath += ".bak";

        try {
            std::filesystem::copy_file(savePath, backupPath,
                std::filesystem::copy_options::overwrite_existing);
            spdlog::info("Created backup: {}", backupPath.string());
        } catch (const std::exception& e) {
            spdlog::warn("Failed to create backup: {}", e.what());
        }
    }

    try {
        ProWriter writer;
        writer.openFile(savePath, true);

        if (writer.write(*_pro)) {
            QMessageBox::information(this, "Success",
                QString("PRO file saved successfully to:\n%1").arg(filePath));
            accept();
        } else {
            QMessageBox::critical(this, "Error",
                "Failed to save PRO file. Check the log for details.");
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error",
            QString("Failed to save PRO file:\n%1").arg(e.what()));
        spdlog::error("ProEditorDialog: Failed to save PRO file: {}", e.what());
    }
}

void ProEditorDialog::onFieldChanged() {
    Q_UNUSED(qobject_cast<QWidget*>(QObject::sender()));
    refreshInfoPanel();
}

void ProEditorDialog::onEditMessageClicked() {
    try {
        const auto* msgFile = ProHelper::msgFile(_resources, _pro->type());
        if (!msgFile) {
            QMessageBox::warning(this, "Message Selection",
                "Could not load MSG file for this object type.");
            return;
        }

        MessageSelectorDialog dialog(msgFile, _pro->header.message_id, this);
        if (dialog.exec() == QDialog::Accepted) {
            int selectedMessageId = dialog.getSelectedMessageId();
            if (selectedMessageId >= 0) {
                _pro->header.message_id = selectedMessageId;

                if (_commonFieldsWidget) {
                    _commonFieldsWidget->loadFromPro(_pro);
                }

                refreshInfoPanel();

                spdlog::debug("ProEditorDialog: Message ID changed to {}", selectedMessageId);
            }
        }

    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Message Selection Error",
            QString("Error opening message selector: %1").arg(e.what()));
        spdlog::error("ProEditorDialog::onEditMessageClicked - Error: {}", e.what());
    }
}

void ProEditorDialog::openFrmSelectorForLabel(QLabel* targetLabel, int32_t* fidStorage, Frm::FRM_TYPE objectType) {
    if (!targetLabel || !fidStorage)
        return;

    FrmSelectorDialog dialog(_resources, this);
    dialog.setObjectTypeFilter(objectType);
    dialog.setInitialFrmPid(static_cast<uint32_t>(*fidStorage));

    if (dialog.exec() == QDialog::Accepted) {
        uint32_t selectedFrmPid = dialog.getSelectedFrmPid();
        if (selectedFrmPid > 0) {
            *fidStorage = static_cast<int32_t>(selectedFrmPid);
            targetLabel->setText(getFrmFilename(*fidStorage));
        }
    }
}

void ProEditorDialog::refreshInfoPanel() {
    if (!_pro || !_infoPanelWidget) {
        return;
    }

    _infoPanelWidget->refreshFromPro(_resources, _pro, static_cast<uint32_t>(_infoPanelWidget->pid()));
    setWindowTitle(_infoPanelWidget->windowTitleText());
}

QString ProEditorDialog::getFrmFilename(int32_t fid) {
    if (fid <= 0) {
        return "No FRM";
    }
    std::string frmPath = _resources.frmResolver().resolve(static_cast<unsigned int>(fid));
    if (frmPath.empty()) {
        return QString("Invalid FID (%1)").arg(fid);
    }
    return QString::fromStdString(std::filesystem::path(frmPath).filename().string());
}

} // namespace geck
