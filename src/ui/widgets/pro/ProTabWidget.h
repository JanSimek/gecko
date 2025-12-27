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
#include <memory>

#include "../../../format/pro/Pro.h"
#include "../../../format/pro/ProDataModels.h"

namespace geck {

/**
 * @brief Base class for PRO editor tab widgets
 *
 * Provides common interface and functionality for all PRO type-specific
 * editor widgets. Each derived class handles a specific PRO type
 * (Armor, Weapon, Critter, etc.)
 *
 * Following the Strategy pattern, each concrete widget implements
 * type-specific UI creation and data handling while sharing common
 * functionality through this base class.
 */
class ProTabWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProTabWidget(QWidget* parent = nullptr);
    virtual ~ProTabWidget() = default;

    /**
     * @brief Load data from PRO file into widget fields
     * @param pro PRO file to load from
     */
    virtual void loadFromPro(const std::shared_ptr<Pro>& pro) = 0;

    /**
     * @brief Save widget field values back to PRO file
     * @param pro PRO file to save to
     */
    virtual void saveToPro(std::shared_ptr<Pro>& pro) = 0;

    /**
     * @brief Check if this widget can handle the given PRO type
     * @param pro PRO file to check
     * @return true if this widget can edit the given PRO type
     */
    virtual bool canHandle(const std::shared_ptr<Pro>& pro) const = 0;

    /**
     * @brief Get the display name for this tab
     * @return Tab label text
     */
    virtual QString getTabLabel() const = 0;

signals:
    /**
     * @brief Emitted when any field value changes
     */
    void fieldChanged();

    /**
     * @brief Emitted when FID selector button is clicked
     * @param targetField The spin box to update with selected FID
     * @param objectType The type of object to select
     */
    void fidSelectorRequested(QSpinBox* targetField, uint32_t objectType);

    /**
     * @brief Emitted when a FID label selector is clicked
     * @param targetLabel The label to update
     * @param fidStorage Pointer to store the selected FID
     * @param objectType The type of object to select
     */
    void fidLabelSelectorRequested(QLabel* targetLabel, int32_t* fidStorage, uint32_t objectType);

protected:
    // Common helper methods for derived classes

    /**
     * @brief Create a standard spin box with common settings
     */
    QSpinBox* createSpinBox(int min, int max, const QString& tooltip = QString());

    /**
     * @brief Create a hex value spin box
     */
    QSpinBox* createHexSpinBox(int max, const QString& tooltip = QString());

    /**
     * @brief Create a combo box with items
     */
    QComboBox* createComboBox(const QStringList& items, const QString& tooltip = QString());

    /**
     * @brief Create a material type combo box
     */
    QComboBox* createMaterialComboBox(const QString& tooltip = QString());

    /**
     * @brief Create a standard form layout
     */
    QFormLayout* createStandardFormLayout();

    /**
     * @brief Create a standard group box
     */
    QGroupBox* createStandardGroupBox(const QString& title);

    /**
     * @brief Connect spin box to emit fieldChanged signal
     */
    void connectSpinBox(QSpinBox* spinBox);

    /**
     * @brief Connect combo box to emit fieldChanged signal
     */
    void connectComboBox(QComboBox* comboBox);

    /**
     * @brief Connect check box to emit fieldChanged signal
     */
    void connectCheckBox(QCheckBox* checkBox);

    /**
     * @brief Load integer array data into spin box widgets
     */
    void loadIntArrayToWidgets(QSpinBox** widgets, const uint32_t* values, int count);

    /**
     * @brief Save spin box widget values to integer array
     */
    void saveWidgetsToIntArray(QSpinBox** widgets, uint32_t* values, int count);

    /**
     * @brief Set combo box index with null check
     */
    void setComboIndex(QComboBox* combo, int index);

    /**
     * @brief Get combo box index with null check
     */
    int getComboIndex(QComboBox* combo, int defaultValue = 0);

    /**
     * @brief Set combo box index with bounds checking
     */
    void setComboIndexSafe(QComboBox* combo, uint32_t index);

    /**
     * @brief Get material type names
     */
    static QStringList getMaterialNames();

    /**
     * @brief Create and configure an array of spinboxes
     * @param array Pointer to array of QSpinBox pointers
     * @param count Number of spinboxes to create
     * @param min Minimum value
     * @param max Maximum value
     * @param tooltipTemplate Template for tooltips ("%1" replaced with label)
     * @param labels Optional labels to use in tooltip instead of index
     */
    void createSpinBoxArray(QSpinBox** array, int count,
        int min, int max,
        const QString& tooltipTemplate = QString(),
        const QStringList& labels = QStringList());

    /**
     * @brief Create a spinbox array with fixed width
     * @param array Pointer to array of QSpinBox pointers
     * @param count Number of spinboxes to create
     * @param min Minimum value
     * @param max Maximum value
     * @param fixedWidth Fixed width for each spinbox
     * @param tooltipTemplate Template for tooltips
     * @param labels Optional labels for tooltips
     */
    void createCompactSpinBoxArray(QSpinBox** array, int count,
        int min, int max, int fixedWidth,
        const QString& tooltipTemplate = QString(),
        const QStringList& labels = QStringList());

protected slots:
    void onFieldChanged();

protected:
    // Layout pointers for derived classes to use
    QVBoxLayout* _mainLayout;

    // Common constants
    static constexpr int LAYOUT_SPACING = 10;
    static constexpr int FORM_SPACING = 6;
};

} // namespace geck
