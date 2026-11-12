#pragma once

#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace geck {

class ModToolsRunner;
struct ModToolResult;

/**
 * Tools > Add Scenery from Picture. A render or photo becomes a new scenery piece through the
 * mod's tools/gecko_assets.py (which uses image_scenery.py): cut out, shaded like Fallout 2's
 * pre-rendered art, put into the game palette, given its own art and proto and a name.
 *
 * The left pane shows the picture; a click there picks the point that stands on the hex. The
 * right pane is the result as the game will draw it, on a hex grid beside the player for scale,
 * redrawn shortly after any change. The piece reaches the palette after Tools > Apply New Art.
 */
class AddSceneryDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddSceneryDialog(ModToolsRunner* runner, QWidget* parent = nullptr);

    /// How many pieces this dialog made (the caller says so when it closes).
    [[nodiscard]] int piecesMade() const { return _piecesMade; }

signals:
    void pieceCreated(const QString& name, const QString& pid);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void choosePicture();
    void loadPicture(const QString& path);
    void pickStandPoint(const QPoint& labelPos);
    void drawSourceView();
    void schedulePreview();
    void runPreview();
    void showPreview(const ModToolResult& result);
    void drawPreview();
    void create();
    void updateCreateButton();
    [[nodiscard]] QStringList pieceArguments() const;
    [[nodiscard]] QString defaultStandLabel() const;

    ModToolsRunner* _runner;

    // the picture
    QString _picturePath;
    QImage _picture;
    std::optional<QPointF> _standPoint; ///< picture pixel that stands on the hex
    double _sourceScale = 1.0;
    QPoint _sourceOffset;

    // the preview
    QString _previewPath;
    QImage _previewImage;
    bool _previewRunning = false;
    bool _previewDirty = false;
    bool _previewOk = false;
    bool _creating = false;
    int _piecesMade = 0;
    QTimer* _previewTimer = nullptr;

    // widgets
    QLineEdit* _pictureEdit = nullptr;
    QLabel* _sourceLabel = nullptr;
    QLabel* _previewLabel = nullptr;
    QComboBox* _zoomCombo = nullptr;
    QLineEdit* _nameEdit = nullptr;
    QLabel* _nameProblemLabel = nullptr;
    QLineEdit* _titleEdit = nullptr;
    QLineEdit* _textEdit = nullptr;
    QComboBox* _kindCombo = nullptr;
    QComboBox* _sizeModeCombo = nullptr;
    QSpinBox* _heightSpin = nullptr;
    QDoubleSpinBox* _scaleSpin = nullptr;
    QComboBox* _standCombo = nullptr;
    QComboBox* _colourCombo = nullptr;
    QCheckBox* _shadowCheck = nullptr;
    QLineEdit* _shadowNameEdit = nullptr;
    QLabel* _infoLabel = nullptr;
    QPushButton* _createButton = nullptr;
};

} // namespace geck
