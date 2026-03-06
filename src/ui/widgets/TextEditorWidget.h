#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include "../../util/Settings.h"

namespace geck {

/**
 * @brief Widget for managing text editor configuration
 *
 * Provides UI for choosing between system default or custom text editor
 * for opening text files from the file browser.
 */
class TextEditorWidget : public QGroupBox {
    Q_OBJECT

public:
    explicit TextEditorWidget(QWidget* parent = nullptr);
    ~TextEditorWidget() = default;

    // Data access
    Settings::TextEditorMode getEditorMode() const;
    void setEditorMode(Settings::TextEditorMode mode);

    QString getCustomEditorPath() const;
    void setCustomEditorPath(const QString& path);

signals:
    void editorModeChanged();
    void customEditorPathChanged();
    void configurationChanged();

private slots:
    void onEditorModeChanged();
    void onCustomEditorPathChanged();
    void onBrowseEditor();

private:
    void setupUI();
    void setupConnections();
    void updateControlStates();

    // UI Components
    QVBoxLayout* _layout;
    QLabel* _helpLabel;
    QRadioButton* _systemEditorRadio;
    QRadioButton* _customEditorRadio;
    QHBoxLayout* _customEditorLayout;
    QLineEdit* _customEditorPathEdit;
    QPushButton* _browseEditorButton;
};

} // namespace geck