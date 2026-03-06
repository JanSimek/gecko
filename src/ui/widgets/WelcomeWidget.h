#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class QSvgRenderer;

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

private:
    void setupUI();
    void renderSvgToLabel(QSvgRenderer& svgRenderer);
    void createVersionLabel();

    QVBoxLayout* _layout;
    QLabel* _imageLabel;
    QLabel* _versionLabel;
};

} // namespace geck