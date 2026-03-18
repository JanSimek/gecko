#include "ProInfoPanelWidget.h"

#include "../../format/msg/Msg.h"
#include "../../format/pro/Pro.h"
#include "../../resource/GameResources.h"
#include "../../util/ProHelper.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"

#include <QAbstractSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace geck {

namespace {

    QString itemTypeLabel(const Pro& pro) {
        switch (pro.itemType()) {
            case Pro::ITEM_TYPE::ARMOR:
                return "Armor";
            case Pro::ITEM_TYPE::CONTAINER:
                return "Container";
            case Pro::ITEM_TYPE::DRUG:
                return "Drug";
            case Pro::ITEM_TYPE::WEAPON:
                return "Weapon";
            case Pro::ITEM_TYPE::AMMO:
                return "Ammo";
            case Pro::ITEM_TYPE::MISC:
                return "Misc Item";
            case Pro::ITEM_TYPE::KEY:
                return "Key";
        }

        return "Item";
    }

    QString objectTypeLabel(const Pro& pro) {
        switch (pro.type()) {
            case Pro::OBJECT_TYPE::ITEM:
                return itemTypeLabel(pro);
            case Pro::OBJECT_TYPE::CRITTER:
                return "Critter";
            case Pro::OBJECT_TYPE::SCENERY:
                return "Scenery";
            case Pro::OBJECT_TYPE::WALL:
                return "Wall";
            case Pro::OBJECT_TYPE::TILE:
                return "Tile";
            case Pro::OBJECT_TYPE::MISC:
                return "Misc";
        }

        return "Object";
    }

    QString fallbackObjectName(uint32_t pid) {
        return QString("Object %1").arg(pid, 8, 16, QChar('0')).toUpper();
    }

}

ProInfoPanelWidget::ProInfoPanelWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void ProInfoPanelWidget::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    _mainLayout->setSpacing(0);

    auto* nameLayout = new QHBoxLayout();
    nameLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _nameLabel = new QLabel(this);
    _nameLabel->setAlignment(Qt::AlignCenter);
    _nameLabel->setWordWrap(true);
    _nameLabel->setStyleSheet(ui::theme::styles::titleLabel());
    nameLayout->addWidget(_nameLabel);

    _editMessageButton = new QPushButton("...", this);
    _editMessageButton->setMaximumWidth(ui::constants::sizes::ICON_BUTTON);
    _editMessageButton->setMaximumHeight(ui::constants::sizes::ICON_BUTTON);
    _editMessageButton->setToolTip("Edit object name and description");
    connect(_editMessageButton, &QPushButton::clicked, this, &ProInfoPanelWidget::editMessageRequested);
    nameLayout->addWidget(_editMessageButton);

    _mainLayout->addLayout(nameLayout);

    _previewContainer = new QWidget(this);
    _previewLayout = new QVBoxLayout(_previewContainer);
    _previewLayout->setContentsMargins(0, ui::constants::SPACING_TIGHT, 0, ui::constants::SPACING_TIGHT);
    _previewLayout->setSpacing(0);
    _mainLayout->addWidget(_previewContainer);

    _descriptionEdit = new QTextEdit(this);
    _descriptionEdit->setFixedHeight(ui::constants::sizes::HEIGHT_DESCRIPTION);
    _descriptionEdit->setReadOnly(true);
    _descriptionEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _descriptionEdit->setStyleSheet(ui::theme::styles::textAreaReadOnly());

    auto* pidLayout = new QHBoxLayout();
    pidLayout->addWidget(new QLabel("PID (hex):", this));

    _pidEdit = new QSpinBox(this);
    _pidEdit->setRange(0, 0x5FFFFFF);
    _pidEdit->setDisplayIntegerBase(16);
    _pidEdit->setToolTip("Object ID and Type (combined 32-bit value)");
    _pidEdit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    _pidEdit->setMinimumWidth(ui::constants::sizes::WIDTH_PID_FIELD_MIN);
    connect(_pidEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProInfoPanelWidget::fieldChanged);
    pidLayout->addWidget(_pidEdit);

    auto* filenameLayout = new QHBoxLayout();
    filenameLayout->addWidget(new QLabel("PID (filename):", this));

    _filenameEdit = new QLineEdit(this);
    _filenameEdit->setReadOnly(true);
    _filenameEdit->setToolTip("PRO filename derived from PID");
    _filenameEdit->setStyleSheet(ui::theme::styles::readOnlyInput());
    _filenameEdit->setMinimumWidth(ui::constants::sizes::WIDTH_PID_FIELD_MIN);
    filenameLayout->addWidget(_filenameEdit);

    _mainLayout->addWidget(_descriptionEdit);
    _mainLayout->addLayout(pidLayout);
    _mainLayout->addLayout(filenameLayout);
    _mainLayout->addStretch();
}

void ProInfoPanelWidget::setPreviewWidget(QWidget* previewWidget) {
    if (_previewWidget == previewWidget) {
        return;
    }

    if (_previewWidget) {
        _previewLayout->removeWidget(_previewWidget);
        _previewWidget->setParent(nullptr);
    }

    _previewWidget = previewWidget;
    if (_previewWidget) {
        _previewWidget->setParent(_previewContainer);
        _previewLayout->addWidget(_previewWidget);
    }
}

void ProInfoPanelWidget::refreshFromPro(resource::GameResources& resources, const std::shared_ptr<Pro>& pro, uint32_t currentPid) {
    if (!pro) {
        setNameAndDescription("No PRO loaded", QString());
        setFilenameText("(Unknown)");
        _windowTitleText = "PRO editor";
        return;
    }

    QString name = fallbackObjectName(currentPid);
    QString description;

    try {
        Msg* msgFile = ProHelper::msgFile(resources, pro->type());
        if (!msgFile) {
            description = "Could not load MSG file for " + QString::fromStdString(pro->typeToString());
            spdlog::warn("ProInfoPanelWidget::refreshFromPro() - MSG file not found for {}", pro->typeToString());
        } else {
            const uint32_t messageId = pro->header.message_id;

            try {
                name = QString::fromStdString(msgFile->message(messageId).text);
            } catch (const std::exception& e) {
                name = "No name (ID: " + QString::number(messageId) + ")";
                spdlog::warn("ProInfoPanelWidget::refreshFromPro() - Missing name {} for type {} ({})",
                    messageId,
                    pro->typeToString(),
                    e.what());
            }

            try {
                description = QString::fromStdString(msgFile->message(messageId + 1).text);
            } catch (const std::exception& e) {
                description = "No description available (ID: " + QString::number(messageId + 1) + ")";
                spdlog::warn("ProInfoPanelWidget::refreshFromPro() - Missing description {} for type {} ({})",
                    messageId + 1,
                    pro->typeToString(),
                    e.what());
            }
        }
    } catch (const std::exception& e) {
        name = "Error loading name";
        description = QString("Error: %1").arg(e.what());
        spdlog::error("ProInfoPanelWidget::refreshFromPro() - Exception: {}", e.what());
    }

    setNameAndDescription(name, description);

    try {
        const std::string proPath = ProHelper::basePath(resources, currentPid);
        const std::string filename = std::filesystem::path(proPath).filename().string();
        setFilenameText(QString::fromStdString(filename));
    } catch (const std::exception& e) {
        setFilenameText("(Unknown)");
        spdlog::warn("ProInfoPanelWidget::refreshFromPro() - Filename error: {}", e.what());
    }

    _windowTitleText = QString("%1 (%2) - PRO editor").arg(name, objectTypeLabel(*pro));
}

void ProInfoPanelWidget::setNameAndDescription(const QString& name, const QString& description) {
    setNameText(name);
    setDescriptionText(description);
}

void ProInfoPanelWidget::setNameText(const QString& name) {
    if (_nameLabel) {
        _nameLabel->setText(name);
    }
}

QString ProInfoPanelWidget::nameText() const {
    return _nameLabel ? _nameLabel->text() : QString();
}

void ProInfoPanelWidget::setDescriptionText(const QString& description) {
    if (_descriptionEdit) {
        _descriptionEdit->setText(description);
    }
}

void ProInfoPanelWidget::setPid(int pid) {
    if (_pidEdit) {
        QSignalBlocker blocker(_pidEdit);
        _pidEdit->setValue(pid);
    }
}

int ProInfoPanelWidget::pid() const {
    return _pidEdit ? _pidEdit->value() : 0;
}

void ProInfoPanelWidget::setFilenameText(const QString& filename) {
    if (_filenameEdit) {
        _filenameEdit->setText(filename);
    }
}

QString ProInfoPanelWidget::filenameText() const {
    return _filenameEdit ? _filenameEdit->text() : QString();
}

QString ProInfoPanelWidget::windowTitleText() const {
    return _windowTitleText;
}

} // namespace geck
