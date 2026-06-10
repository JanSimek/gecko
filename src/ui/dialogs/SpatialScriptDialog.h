#pragma once

#include <QDialog>

#include <vector>
#include <string>

class QLabel;
class QSpinBox;
class QComboBox;
class QPushButton;

namespace geck {

/// @brief Creates a spatial (hex trigger-zone) script: a scripts.lst program
/// placed at a hex with a radius. Mirrors the engine's map_scr_add_spatial - the
/// script record carries the built-tile position and radius; no saved object is
/// involved (the engine's hex marker is editor-only / NO_SAVE).
class SpatialScriptDialog : public QDialog {
    Q_OBJECT

public:
    explicit SpatialScriptDialog(const std::vector<std::string>& scriptNames, QWidget* parent = nullptr);

    int programIndex() const { return _programIndex; }
    int tile() const;
    int elevation() const;
    int radius() const;

    static constexpr int MAX_HEX_TILE = 39999;
    static constexpr int MAX_RADIUS = 50;

private slots:
    void onChooseScript();

private:
    std::vector<std::string> _scriptNames;
    int _programIndex = -1;

    QLabel* _scriptLabel;
    QSpinBox* _tileSpin;
    QComboBox* _elevationCombo;
    QSpinBox* _radiusSpin;
    QPushButton* _okButton;
};

} // namespace geck
