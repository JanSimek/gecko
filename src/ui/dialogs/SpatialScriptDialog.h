#pragma once

#include "ui/common/BaseDialog.h"
#include "ui/dialogs/ScriptSelectorDialog.h"

#include <vector>

class QLabel;
class QSpinBox;
class QComboBox;
class QPushButton;

namespace geck {

/// @brief Creates a spatial (hex trigger-zone) script: a scripts.lst program
/// placed at a hex with a radius. Mirrors the engine's map_scr_add_spatial - the
/// script record carries the built-tile position and radius; no saved object is
/// involved (the engine's hex marker is editor-only / NO_SAVE).
class SpatialScriptDialog : public BaseDialog {
    Q_OBJECT

public:
    explicit SpatialScriptDialog(const std::vector<ScriptSelectorDialog::Entry>& scripts, QWidget* parent = nullptr);

    int programIndex() const { return _programIndex; }
    int tile() const;
    int elevation() const;
    int radius() const;

    // Pre-populate the fields so the dialog can edit an existing spatial script. setProgramIndex
    // also updates the script label and enables OK (a chosen script is what OK requires).
    void setProgramIndex(int index);
    void setTile(int tile);
    void setElevation(int elevation);
    void setRadius(int radius);

    static constexpr int MAX_RADIUS = 50; // engine win_get_num_i range in map_scr_add_spatial

signals:
    /// The user clicked "Pick on map"; the host should let them click a hex and feed it back via
    /// setTile()/setElevation(). The dialog is non-modal so the map stays interactive.
    void pickPositionRequested();

private slots:
    void onChooseScript();

private:
    // Sets _programIndex, refreshes the script label from _scripts, and enables OK.
    void selectProgram(int index);
    std::vector<ScriptSelectorDialog::Entry> _scripts;
    int _programIndex = -1;

    QLabel* _scriptLabel;
    QSpinBox* _tileSpin;
    QComboBox* _elevationCombo;
    QSpinBox* _radiusSpin;
    QPushButton* _okButton;
};

} // namespace geck
