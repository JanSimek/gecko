#pragma once

#include "ProTabWidget.h"
#include <QPushButton>
#include "../ObjectPreviewWidget.h"

namespace geck {

/**
 * @brief Widget for editing Armor type PRO files
 *
 * Handles armor-specific fields including:
 * - Armor Class
 * - Damage Threshold and Resistance for 7 damage types
 * - Armor perk
 * - Male/Female armor FIDs
 * - AI Priority calculation
 */
class ProArmorWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProArmorWidget(QWidget* parent = nullptr);
    ~ProArmorWidget() override = default;

    // ProTabWidget interface
    void loadFromPro(const std::shared_ptr<Pro>& pro) override;
    void saveToPro(std::shared_ptr<Pro>& pro) override;
    bool canHandle(const std::shared_ptr<Pro>& pro) const override;
    QString getTabLabel() const override;

    // Armor FID management
    void setArmorMaleFID(int32_t fid);
    void setArmorFemaleFID(int32_t fid);

signals:

private:
    void setupUI();
    void updateAIPriority();
    void updateArmorPreviews();
    int calculateAIPriority() const;

    // UI controls
    QSpinBox* _armorClassEdit;
    QSpinBox* _damageResistEdits[7];
    QSpinBox* _damageThresholdEdits[7];
    QComboBox* _armorPerkCombo;
    QLabel* _armorAIPriorityLabel;

    // Preview widgets
    ObjectPreviewWidget* _armorMalePreviewWidget;
    ObjectPreviewWidget* _armorFemalePreviewWidget;

    // Data
    ProArmorData _armorData;
    int32_t _armorMaleFID;
    int32_t _armorFemaleFID;

    // Constants
    static constexpr int DAMAGE_TYPES_COUNT = 7;
};

} // namespace geck