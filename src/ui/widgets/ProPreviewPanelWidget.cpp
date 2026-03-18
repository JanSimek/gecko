#include "ProPreviewPanelWidget.h"

#include "ObjectPreviewWidget.h"
#include "../dialogs/FrmSelectorDialog.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"
#include "../../format/pro/Pro.h"
#include "../../resource/GameResources.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace geck {

ProPreviewPanelWidget::ProPreviewPanelWidget(resource::GameResources& resources, const std::shared_ptr<Pro>& pro, QWidget* parent)
    : QWidget(parent)
    , _pro(pro)
    , _resources(resources) {
    setupUI();
}

void ProPreviewPanelWidget::setupUI() {
    setContentsMargins(0, 0, 0, 0);
    setStyleSheet(ui::theme::styles::compactWidget());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignCenter);

    if (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM) {
        auto* dualWidget = new QWidget(this);
        auto* dualLayout = new QHBoxLayout(dualWidget);
        dualLayout->setContentsMargins(0, 0, 0, 0);
        dualLayout->setSpacing(ui::constants::SPACING_TIGHT);
        dualLayout->setAlignment(Qt::AlignCenter);

        _inventoryPreviewWidget = new ObjectPreviewWidget(_resources, dualWidget,
            ObjectPreviewWidget::PreviewOptions(),
            QSize(ITEM_PREVIEW_SIZE, ITEM_PREVIEW_SIZE));
        _groundPreviewWidget = new ObjectPreviewWidget(_resources, dualWidget,
            ObjectPreviewWidget::PreviewOptions(),
            QSize(ITEM_PREVIEW_SIZE, ITEM_PREVIEW_SIZE));

        connect(_inventoryPreviewWidget, &ObjectPreviewWidget::fidChangeRequested,
            this, &ProPreviewPanelWidget::onPreviewFidChangeRequested);
        connect(_groundPreviewWidget, &ObjectPreviewWidget::fidChangeRequested,
            this, &ProPreviewPanelWidget::onPreviewFidChangeRequested);

        dualLayout->addWidget(_inventoryPreviewWidget);
        dualLayout->addWidget(_groundPreviewWidget);
        layout->addWidget(dualWidget);
        return;
    }

    _objectPreviewWidget = new ObjectPreviewWidget(_resources, this);
    connect(_objectPreviewWidget, &ObjectPreviewWidget::fidChangeRequested,
        this, &ProPreviewPanelWidget::onPreviewFidChangeRequested);
    layout->addWidget(_objectPreviewWidget);
}

void ProPreviewPanelWidget::refresh() {
    if (!_pro) {
        return;
    }

    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        refreshInventoryPreview();
        refreshGroundPreview();
        return;
    }

    refreshObjectPreview();
}

void ProPreviewPanelWidget::refreshObjectPreview() {
    applyPreview(_objectPreviewWidget, _pro ? _pro->header.FID : 0);
}

void ProPreviewPanelWidget::refreshInventoryPreview() {
    applyPreview(_inventoryPreviewWidget, inventoryFid());
}

void ProPreviewPanelWidget::refreshGroundPreview() {
    applyPreview(_groundPreviewWidget, groundFid());
}

void ProPreviewPanelWidget::applyPreview(ObjectPreviewWidget* previewWidget, int32_t fid) {
    if (!previewWidget) {
        return;
    }

    if (fid <= 0) {
        previewWidget->clear();
        return;
    }

    try {
        const std::string frmPath = _resources.frmResolver().resolve(static_cast<uint32_t>(fid));
        if (frmPath.empty()) {
            previewWidget->clear();
            return;
        }

        previewWidget->setFrmPath(QString::fromStdString(frmPath));
        previewWidget->setFid(fid);
    } catch (const std::exception& e) {
        spdlog::error("ProPreviewPanelWidget::applyPreview() - exception: {}", e.what());
        previewWidget->clear();
    }
}

int32_t ProPreviewPanelWidget::inventoryFid() const {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM) {
        return 0;
    }

    return _pro->commonItemData.inventoryFID;
}

int32_t ProPreviewPanelWidget::groundFid() const {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM) {
        return 0;
    }

    return _pro->header.FID;
}

void ProPreviewPanelWidget::onPreviewFidChangeRequested() {
    if (!_pro) {
        return;
    }

    auto* senderWidget = qobject_cast<ObjectPreviewWidget*>(sender());
    if (!senderWidget) {
        return;
    }

    FrmSelectorDialog dialog(_resources, this);
    dialog.setObjectTypeFilter(FrmSelectorDialog::filterForObjectType(_pro->type()));

    uint32_t initialFid = 0;
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        if (senderWidget == _inventoryPreviewWidget) {
            const int32_t currentInventoryFid = inventoryFid();
            initialFid = static_cast<uint32_t>(currentInventoryFid > 0 ? currentInventoryFid : groundFid());
        } else {
            initialFid = static_cast<uint32_t>(std::max(groundFid(), 0));
        }
    } else {
        initialFid = static_cast<uint32_t>(std::max(_pro->header.FID, 0));
    }

    dialog.setInitialFrmPid(initialFid);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const uint32_t selectedFrmPid = dialog.getSelectedFrmPid();
    if (selectedFrmPid == 0) {
        return;
    }

    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        if (senderWidget == _inventoryPreviewWidget) {
            emit inventoryFidSelected(static_cast<int32_t>(selectedFrmPid));
        } else if (senderWidget == _groundPreviewWidget) {
            emit groundFidSelected(static_cast<int32_t>(selectedFrmPid));
        }
        refresh();
        return;
    }

    emit objectFidSelected(static_cast<int32_t>(selectedFrmPid));
    refresh();
}

} // namespace geck
