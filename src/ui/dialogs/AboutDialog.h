#pragma once

#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

namespace geck {

/**
 * @brief About dialog showing application information
 *
 * Displays application name, version, description, and icon
 * in a standard About dialog format.
 */
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
    ~AboutDialog() = default;

private:
    void setupUI();
    void createContent();
    void createButtons();

    QVBoxLayout* _mainLayout;
    QHBoxLayout* _contentLayout;
    QVBoxLayout* _textLayout;
    QHBoxLayout* _buttonLayout;

    QLabel* _iconLabel;
    QLabel* _titleLabel;
    QLabel* _descriptionLabel;
    QPushButton* _okButton;
};

} // namespace geck