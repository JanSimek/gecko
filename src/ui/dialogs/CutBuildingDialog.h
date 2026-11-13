#pragma once

#include <QDialog>
#include <QImage>
#include <QList>
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
 * Tools > Cut Building into Walls. A building render becomes Fallout wall pieces, one per hex,
 * cut where the original walls are cut, through the mod's tools/gecko_assets.py (which uses
 * slice_building.py), plus a stamp in the patterns' Custom folder that lays them all down.
 *
 * The left pane is the picture: clicks mark the foot of the building from left to right, one
 * point at each end and one at each corner (right-click takes the last one back). With fewer
 * than two points the tool guesses the foot, and the guess is drawn in a second colour. With
 * "Roof as roof tiles", everything above the wall height (a Shift-click on a wall's top sets it;
 * the orange line shows it) becomes roof tiles instead of wall. The right pane is the building
 * as the game draws it beside the player, and where it is cut.
 */
class CutBuildingDialog : public QDialog {
    Q_OBJECT

public:
    explicit CutBuildingDialog(ModToolsRunner* runner, QWidget* parent = nullptr);

    [[nodiscard]] int buildingsMade() const { return _buildingsMade; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void choosePicture();
    void loadPicture(const QString& path);
    void addPoint(const QPoint& labelPos);
    void removeLastPoint();
    void setWallTopFrom(const QPoint& labelPos);
    /// The foot's y (picture pixels) at picture column x, from the clicked points or the guess,
    /// the end stretches carried on past the ends; nullopt with no foot yet.
    [[nodiscard]] std::optional<double> footAt(double x) const;
    void drawSourceView();
    void schedulePreview();
    void runPreview();
    void showPreview(const ModToolResult& result);
    void drawPreview();
    void create();
    void updateCreateButton();
    [[nodiscard]] QStringList buildingArguments() const;

    ModToolsRunner* _runner;

    QString _picturePath;
    QImage _picture;
    QList<QPointF> _points;  ///< the foot, clicked, in picture pixels
    QList<QPointF> _guessed; ///< the foot the tool used when fewer than two were clicked
    double _sourceScale = 1.0;
    QPoint _sourceOffset;

    QString _previewPath;
    QImage _previewImage;
    bool _previewRunning = false;
    bool _previewDirty = false;
    bool _previewOk = false;
    bool _creating = false;
    int _buildingsMade = 0;
    QTimer* _previewTimer = nullptr;

    QLineEdit* _pictureEdit = nullptr;
    QLabel* _sourceLabel = nullptr;
    QLabel* _previewLabel = nullptr;
    QComboBox* _zoomCombo = nullptr;
    QLineEdit* _prefixEdit = nullptr;
    QLineEdit* _stampEdit = nullptr;
    QLabel* _problemLabel = nullptr;
    QComboBox* _sizeModeCombo = nullptr;
    QSpinBox* _heightSpin = nullptr;
    QDoubleSpinBox* _scaleSpin = nullptr;
    QComboBox* _colourCombo = nullptr;
    QCheckBox* _roofCheck = nullptr;
    QDoubleSpinBox* _wallTopSpin = nullptr; ///< walls' height above the foot, picture pixels
    QLabel* _wallTopGameLabel = nullptr;
    bool _wallTopAuto = true;               ///< ask the tool to guess until it is set
    QLabel* _infoLabel = nullptr;
    QPushButton* _createButton = nullptr;
};

} // namespace geck
