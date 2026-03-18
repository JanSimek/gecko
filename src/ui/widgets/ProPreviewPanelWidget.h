#pragma once

#include <QWidget>

#include <cstdint>
#include <memory>

namespace geck {

class ObjectPreviewWidget;
class Pro;

namespace resource {
    class GameResources;
}

class ProPreviewPanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProPreviewPanelWidget(resource::GameResources& resources, const std::shared_ptr<Pro>& pro, QWidget* parent = nullptr);
    ~ProPreviewPanelWidget() override = default;

    void refresh();

signals:
    void inventoryFidSelected(int32_t fid);
    void groundFidSelected(int32_t fid);
    void objectFidSelected(int32_t fid);

private slots:
    void onPreviewFidChangeRequested();

private:
    void setupUI();
    void refreshObjectPreview();
    void refreshInventoryPreview();
    void refreshGroundPreview();
    void applyPreview(ObjectPreviewWidget* previewWidget, int32_t fid);
    int32_t inventoryFid() const;
    int32_t groundFid() const;

    static constexpr int ITEM_PREVIEW_SIZE = 120;

    ObjectPreviewWidget* _objectPreviewWidget = nullptr;
    ObjectPreviewWidget* _inventoryPreviewWidget = nullptr;
    ObjectPreviewWidget* _groundPreviewWidget = nullptr;

    std::shared_ptr<const Pro> _pro;
    resource::GameResources& _resources;
};

} // namespace geck
