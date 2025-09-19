#pragma once

#include "ProTabWidget.h"

namespace geck {

/**
 * @brief Widget for editing Container and Key type PRO files
 * 
 * Handles both:
 * - Container-specific fields: max size, flags (Use, UseOn, Look, Talk, Pickup)
 * - Key-specific fields: key ID
 * 
 * Uses two-column layout for containers, single field for keys
 */
class ProContainerKeyWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProContainerKeyWidget(QWidget* parent = nullptr);
    ~ProContainerKeyWidget() override = default;

    // ProTabWidget interface
    void loadFromPro(const std::shared_ptr<Pro>& pro) override;
    void saveToPro(std::shared_ptr<Pro>& pro) override;
    bool canHandle(const std::shared_ptr<Pro>& pro) const override;
    QString getTabLabel() const override;

private:
    void setupUI();
    void setupContainerUI();
    void setupKeyUI();
    
    // UI controls - Container
    QSpinBox* _containerMaxSizeEdit;
    QCheckBox* _containerFlagChecks[5];  // Use, UseOn, Look, Talk, Pickup
    
    // UI controls - Key  
    QSpinBox* _keyIdEdit;
    
    // Data
    ProContainerData _containerData;
    ProKeyData _keyData;
    
    // Type tracking
    bool _isContainer;
    bool _isKey;
    
    // Constants
    static constexpr int NUM_CONTAINER_FLAGS = 5;
};

} // namespace geck