#pragma once

#include <QDialog>
#include <QDialogButtonBox>

namespace geck {

/**
 * @brief Base class for dialog windows
 *
 * Provides common dialog functionality:
 * - Standard window configuration (modal, no help button)
 * - OK/Cancel button box creation and connection
 */
class BaseDialog : public QDialog {
    Q_OBJECT

public:
    explicit BaseDialog(const QString& title, QWidget* parent = nullptr);
    virtual ~BaseDialog() = default;

protected:
    /**
     * @brief Creates an OK/Cancel button box wired to accept() / reject()
     * @return The created button box, for the caller to add to its layout
     */
    [[nodiscard]] QDialogButtonBox* createButtonBox();

    /**
     * @brief Standard dialog setup - removes help button, sets modal
     */
    void setupDialogDefaults();
};

} // namespace geck
