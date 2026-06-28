#pragma once

#include <QDialog>
#include <QPointer>

#include <string>

#include "scripting/EditArea.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSpinBox;
class QTimer;

namespace geck {

class EditorWidget;

/// "Fill Selection…": a one-shot procedural area fill over the current selection (no EditorMode).
/// Left, a browser of Luau fill scripts (bundled examples + the user's library); right, a seed
/// (defaults random, can be locked) and a live-preview toggle. The chosen script runs into a ghost
/// overlay on the editor (debounced, recomputed only when the seed/script changes); Apply commits that
/// exact previewed plan as one undo entry. The dialog drives EditorWidget::previewLuaFill /
/// applyFillPreview / clearFillPreview and never mutates the map itself. The fill feature is
/// scripting-only, so MainWindow offers it only when scripting is compiled in (GECK_SCRIPTING_ENABLED).
class FillDialog : public QDialog {
    Q_OBJECT

public:
    FillDialog(EditorWidget& editor, QWidget* parent = nullptr);
    ~FillDialog() override;

private slots:
    void onScriptActivated(QListWidgetItem* item);
    void onRandomizeSeed();
    void onApply();

private:
    void buildUi();
    void populateBrowser();
    void schedulePreview();
    void runPreview();

    // QPointer (not a reference): the editor is owned by MainWindow and could in principle be
    // deleteLater()'d while this dialog's nested event loop runs; QPointer auto-nulls on its
    // destruction, so every use is guarded rather than dangling.
    QPointer<EditorWidget> _editor;
    EditArea _area; ///< the fill target, snapshotted from the selection at construction

    QListWidget* _browser = nullptr;
    QSpinBox* _seed = nullptr;
    QCheckBox* _seedLock = nullptr;
    QPushButton* _randomizeSeed = nullptr;
    QCheckBox* _livePreview = nullptr;
    QLabel* _summary = nullptr;
    QPushButton* _applyButton = nullptr;
    QTimer* _previewTimer = nullptr;

    QString _selectedName;     ///< display name of the chosen fill, used in the undo description
    std::string _scriptSource; ///< source of the selected Luau fill script (empty until one is picked)
};

} // namespace geck
