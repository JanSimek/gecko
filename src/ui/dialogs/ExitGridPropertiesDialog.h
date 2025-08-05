#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

namespace geck {

/**
 * @brief Properties dialog for exit grid configuration
 * 
 * Provides UI for setting the 4 essential exit grid properties:
 * - Destination map ID
 * - Player spawn position (hex coordinate)
 * - Destination elevation level
 * - Player orientation/direction
 */
class ExitGridPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    struct ExitGridProperties {
        uint32_t exitMap = 0;           // Destination map ID
        uint32_t exitPosition = 0;      // Player spawn position (0-39999)
        uint32_t exitElevation = 0;     // Destination elevation (0-2)
        uint32_t exitOrientation = 0;   // Player direction (0-5)
    };

    explicit ExitGridPropertiesDialog(QWidget* parent = nullptr);
    explicit ExitGridPropertiesDialog(const ExitGridProperties& properties, QWidget* parent = nullptr);
    ~ExitGridPropertiesDialog() = default;

    ExitGridProperties getProperties() const;
    void setProperties(const ExitGridProperties& properties);

private slots:
    void onAccept();
    void onPositionChanged();
    void validateInput();

private:
    void setupUI();
    void setupFormLayout();
    void setupButtonBox();
    void initializeDefaults();
    void updateUI();
    bool isValidInput() const;

    // UI Components
    QVBoxLayout* _mainLayout;
    QFormLayout* _formLayout;
    QDialogButtonBox* _buttonBox;

    // Input fields
    QSpinBox* _mapIdSpinBox;
    QSpinBox* _positionSpinBox;
    QComboBox* _elevationComboBox;
    QComboBox* _orientationComboBox;
    
    // Status
    QLabel* _statusLabel;

    // Properties
    ExitGridProperties _properties;
};

} // namespace geck