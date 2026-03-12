#pragma once

#include "ProTabWidget.h"

namespace geck {

/**
 * @brief Widget for editing Wall type PRO files
 *
 * Walls are the simplest PRO type, containing only a material ID
 * field after the common header. This makes it an ideal starting
 * point for the refactoring.
 */
class ProWallWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProWallWidget(resource::GameResources& resources, QWidget* parent = nullptr);
    ~ProWallWidget() override = default;

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
    ProWallData _wallData;
};

} // namespace geck
