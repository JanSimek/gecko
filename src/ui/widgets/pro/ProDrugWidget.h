#pragma once

#include "ProTabWidget.h"

namespace geck {

/**
 * @brief Widget for editing Drug type PRO files
 *
 * Handles drug-specific fields including:
 * - Stat effects (immediate, mid-time, long-time) for up to 3 stats
 * - Effect timing (mid-time and long-time delays)
 * - Addiction settings (rate, effect perk, onset delay)
 *
 * Uses single-column layout as drugs have many complex controls
 */
class ProDrugWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProDrugWidget(resource::GameResources& resources, QWidget* parent = nullptr);
    ~ProDrugWidget() override = default;

    // ProTabWidget interface
    void loadFromPro(const std::shared_ptr<Pro>& pro) override;
    void saveToPro(std::shared_ptr<Pro>& pro) override;
    bool canHandle(const std::shared_ptr<Pro>& pro) const override;
    QString getTabLabel() const override;

    // Set stat and perk names for dropdown lists
    void setStatNames(const QStringList& statNames);
    void setPerkOptions(const QVector<game::enums::EnumOption>& perkOptions);

private:
    void setupUI();

    // Stat names and perk names for combo boxes
    QStringList _statNames;
    QVector<game::enums::EnumOption> _perkOptions;

    // UI controls - Stat Effects (3 stats x 4 fields each)
    QComboBox* _drugStatCombos[3];           // Which stat to modify
    QSpinBox* _drugStatAmountEdits[3];       // Immediate effect amount
    QSpinBox* _drugFirstStatAmountEdits[3];  // Mid-time effect amount
    QSpinBox* _drugSecondStatAmountEdits[3]; // Long-time effect amount

    // UI controls - Effect Timing
    QSpinBox* _drugFirstDelayEdit;  // Mid-time delay
    QSpinBox* _drugSecondDelayEdit; // Long-time delay

    // UI controls - Addiction
    QSpinBox* _drugAddictionChanceEdit; // Addiction chance percentage
    QComboBox* _drugAddictionPerkCombo; // Addiction perk effect
    QSpinBox* _drugAddictionDelayEdit;  // Addiction onset delay

    // Data
    ProDrugData _drugData;

    // Constants
    static constexpr int NUM_DRUG_STATS = 3;
};

} // namespace geck
