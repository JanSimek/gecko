#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include "../theme/ThemeManager.h"

namespace geck {

/**
 * @brief Base class for dialog windows
 *
 * Provides common dialog functionality following the BasePanel pattern:
 * - Standard window configuration (modal, no help button)
 * - Button box creation and connection
 * - Layout helpers
 */
class BaseDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Dialog button configuration
     */
    enum ButtonConfig {
        OkCancel, // Standard OK/Cancel
        OkOnly,   // Just OK button
        Custom    // No automatic buttons
    };

    explicit BaseDialog(const QString& title,
        QWidget* parent = nullptr,
        ButtonConfig buttons = OkCancel);
    virtual ~BaseDialog() = default;

protected:
    /**
     * @brief Creates the main layout for the dialog
     * @return The created layout
     */
    [[nodiscard]] QVBoxLayout* createMainLayout();

    /**
     * @brief Creates and configures the button box
     * @param config Button configuration
     * @return The created button box (or nullptr for Custom)
     */
    [[nodiscard]] QDialogButtonBox* createButtonBox(ButtonConfig config = OkCancel);

    /**
     * @brief Standard dialog setup - removes help button, sets modal
     */
    void setupDialogDefaults();

    /**
     * @brief Set dialog size with minimum and preferred sizes
     */
    void setDialogSize(int minWidth, int minHeight,
        int prefWidth, int prefHeight);

    /**
     * @brief Set dialog size (preferred only, no minimum)
     */
    void setDialogSize(int width, int height);

    /**
     * @brief Set fixed dialog size (no resize allowed)
     */
    void setFixedDialogSize(int width, int height);

    // Protected members
    QVBoxLayout* _mainLayout = nullptr;
    QDialogButtonBox* _buttonBox = nullptr;
};

} // namespace geck
