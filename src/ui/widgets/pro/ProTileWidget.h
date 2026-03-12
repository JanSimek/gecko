#pragma once

#include "ProTabWidget.h"

namespace geck {

/**
 * @brief Widget for editing Tile type PRO files
 *
 * Tiles are simple PRO types, containing only a material ID
 * field after the common header, similar to walls.
 */
class ProTileWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProTileWidget(resource::GameResources& resources, QWidget* parent = nullptr);
    ~ProTileWidget() override = default;

    // ProTabWidget interface
    void loadFromPro(const std::shared_ptr<Pro>& pro) override;
    void saveToPro(std::shared_ptr<Pro>& pro) override;
    bool canHandle(const std::shared_ptr<Pro>& pro) const override;
    QString getTabLabel() const override;

private:
    void setupUI();

    // UI controls
    QComboBox* _materialIdCombo;

    // Data
    ProTileData _tileData;
};

} // namespace geck
