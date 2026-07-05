#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class QSvgRenderer;
class QPushButton;

namespace geck {

/**
 * @brief Welcome screen widget shown when no map is loaded
 *
 * Displays the Vault Boy image in the center of the editing area
 * when no map is currently loaded in the editor.
 */
class WelcomeWidget : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeWidget(QWidget* parent = nullptr);
    ~WelcomeWidget() = default;

signals:
    /// The user asked to create a new map from the welcome screen.
    void newMapRequested();
    /// The user asked to open the map browser from the welcome screen.
    void browseMapsRequested();
    /// The user asked to open the preferences dialog from the welcome screen.
    void preferencesRequested();

private:
    void setupUI();
    void renderSvgToLabel(QSvgRenderer& svgRenderer);
    void createVersionLabel();
    void createActionButtons();

    QVBoxLayout* _layout;
    QLabel* _imageLabel;
    QLabel* _versionLabel;
    QPushButton* _newMapButton;
    QPushButton* _browseButton;
    QPushButton* _preferencesButton;
};

} // namespace geck