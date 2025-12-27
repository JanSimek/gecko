#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <memory>
#include <climits>

#include "../../format/pro/Pro.h"
#include "../../format/msg/Msg.h"

namespace geck {

/**
 * @brief Reusable widget for editing PRO file common fields
 *
 * This widget handles the 56-byte common header that appears in all PRO file types:
 * - ObjectType & ObjectID (PID) - 4 bytes
 * - TextID (Message ID) - 4 bytes
 * - FrmType & FrmID (FID) - 4 bytes
 * - Light Radius - 4 bytes
 * - Light Intensity - 4 bytes
 * - Flags - 4 bytes (+ extended flags)
 * - Additional common item fields (when applicable)
 *
 * Follows DRY principle by consolidating common PRO editing functionality
 * and KISS principle with straightforward, focused interface.
 */
class ProCommonFieldsWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProCommonFieldsWidget(QWidget* parent = nullptr);
    ~ProCommonFieldsWidget() = default;

    /**
     * @brief Load data from PRO file into widget fields
     * @param pro PRO file to load from
     */
    void loadFromPro(const std::shared_ptr<Pro>& pro);

    /**
     * @brief Save widget field values back to PRO file
     * @param pro PRO file to save to
     */
    void saveToPro(std::shared_ptr<Pro>& pro);

    /**
     * @brief Get current PID value
     */
    int32_t getPID() const;

    /**
     * @brief Set PID value
     */
    void setPID(int32_t pid);

    /**
     * @brief Enable/disable item-specific fields based on object type
     * @param isItem True if this is an item type (armor, weapon, etc.)
     */
    void setItemFieldsVisible(bool isItem);

    // UI Constants (Data Type Limits and Documented Game Limits Only)
    static constexpr int MAX_PID_VALUE = 0x5FFFFFF;    // 24-bit object ID
    static constexpr int MAX_FID_VALUE = 0xFFFFFF;     // 24-bit frame ID
    static constexpr int MAX_LIGHT_RADIUS = 8;         // Wiki: "0..8 (hexes)"
    static constexpr int MAX_LIGHT_INTENSITY = 65536;  // Wiki: "0..65536 (0x0000FFFF)"
    static constexpr int MAX_FLAGS_VALUE = 0xFFFFFFFF; // 32-bit flags
    static constexpr int MAX_SOUND_ID = 255;           // 8-bit sound ID

signals:
    /**
     * @brief Emitted when any field value changes
     */
    void fieldChanged();

private slots:
    void onFieldChanged();
    void onObjectFlagChanged();
    void onExtendedFlagChanged();

private:
    void setupUI();
    void setupBasicFields(QFormLayout* layout);
    void setupLightingFields(QFormLayout* layout);
    void setupObjectFlags(QFormLayout* layout);
    void setupExtendedFlags(QFormLayout* layout);
    void setupItemFields(QFormLayout* layout);

    void loadObjectFlags(uint32_t flags);
    void loadExtendedFlags(uint32_t flagsExt);
    uint32_t saveObjectFlags() const;
    uint32_t saveExtendedFlags() const;

    // Helper methods following DRY principle
    QSpinBox* createSpinBox(int min, int max, const QString& tooltip = QString());
    QSpinBox* createHexSpinBox(int max, const QString& tooltip = QString());
    QComboBox* createMaterialComboBox(const QString& tooltip = QString());
    void connectSpinBox(QSpinBox* spinBox);
    void connectComboBox(QComboBox* comboBox);
    void connectCheckBox(QCheckBox* checkBox);

    // UI Components
    QVBoxLayout* _mainLayout;
    // QGroupBox* _basicFieldsGroup;  // Commented out - not used (PID moved to main dialog)
    QGroupBox* _lightingGroup;
    QGroupBox* _objectFlagsGroup;
    QGroupBox* _extendedFlagsGroup;
    QGroupBox* _itemFieldsGroup; // Only visible for items

    // Basic Fields (PID moved to main dialog)

    // Lighting Fields
    QSpinBox* _lightRadiusEdit;
    QSpinBox* _lightIntensityEdit;

    // Object Flags (core engine flags)
    QCheckBox* _flatCheck;        // 0x00000008 - Flat (rendered with tiles)
    QCheckBox* _noBlockCheck;     // 0x00000010 - NoBlock (doesn't block movement)
    QCheckBox* _lightingCheck;    // 0x00000020 - Has lighting
    QCheckBox* _multiHexCheck;    // 0x00000800 - MultiHex (occupies multiple hexes)
    QCheckBox* _noHighlightCheck; // 0x00001000 - No Highlight
    QCheckBox* _transRedCheck;    // 0x00004000 - Red transparency
    QCheckBox* _transNoneCheck;   // 0x00008000 - No transparency (opaque)
    QCheckBox* _transWallCheck;   // 0x00010000 - Wall transparency
    QCheckBox* _transGlassCheck;  // 0x00020000 - Glass transparency
    QCheckBox* _transSteamCheck;  // 0x00040000 - Steam transparency
    QCheckBox* _transEnergyCheck; // 0x00080000 - Energy transparency
    QCheckBox* _lightThruCheck;   // 0x20000000 - Light passes through
    QCheckBox* _shootThruCheck;   // 0x80000000 - Can shoot through

    // Extended Flags (modding/extended features)
    QSpinBox* _animationPrimaryEdit;   // Primary attack animation (bits 0-3)
    QSpinBox* _animationSecondaryEdit; // Secondary attack animation (bits 4-7)

    // Item-specific Common Fields (only for item types)
    QSpinBox* _sidEdit;           // Script ID
    QComboBox* _materialCombo;    // Material type
    QSpinBox* _containerSizeEdit; // Container/inventory size
    QSpinBox* _weightEdit;        // Weight (pounds * 16)
    QSpinBox* _basePriceEdit;     // Base price in caps
    QSpinBox* _soundIdEdit;       // Sound effect ID

    // Data storage
    std::shared_ptr<Pro> _pro;
};

} // namespace geck