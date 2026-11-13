#include "CutBuildingDialog.h"

#include "ui/QtDialogs.h"
#include "ui/assets/ModToolsRunner.h"
#include "ui/theme/ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace geck {

using namespace ui::constants;

namespace {

    constexpr int SOURCE_W = 380;
    constexpr int SOURCE_H = 320;
    constexpr int PREVIEW_DELAY_MS = 500;
    constexpr int MAX_POINTS = 8;

    const QColor PANE_BACKGROUND(40, 36, 30);
    const QColor CLICKED(225, 200, 90);
    const QColor GUESSED(90, 200, 220);
    const QColor WALL_TOP(240, 150, 40);

} // namespace

CutBuildingDialog::CutBuildingDialog(ModToolsRunner* runner, QWidget* parent)
    : QDialog(parent)
    , _runner(runner)
    , _previewPath(QDir(QDir::tempPath()).filePath(
          QString("gecko-cut-building-%1.png").arg(QCoreApplication::applicationPid()))) {
    setWindowTitle("Cut Building into Walls");
    setModal(true);
    setupUI();

    _previewTimer = new QTimer(this);
    _previewTimer->setSingleShot(true);
    _previewTimer->setInterval(PREVIEW_DELAY_MS);
    connect(_previewTimer, &QTimer::timeout, this, &CutBuildingDialog::runPreview);

    drawSourceView();
    updateCreateButton();
}

void CutBuildingDialog::setupUI() {
    auto* main = new QVBoxLayout(this);
    main->setContentsMargins(ui::theme::spacing::LOOSE, ui::theme::spacing::LOOSE, ui::theme::spacing::LOOSE, ui::theme::spacing::LOOSE);
    main->setSpacing(ui::theme::spacing::LOOSE);

    auto* pictureRow = new QHBoxLayout();
    pictureRow->addWidget(new QLabel("Render:"));
    _pictureEdit = new QLineEdit();
    _pictureEdit->setReadOnly(true);
    _pictureEdit->setPlaceholderText("A building rendered with the Fallout camera, transparent or plain white background");
    pictureRow->addWidget(_pictureEdit, 1);
    auto* chooseButton = new QPushButton("Choose...");
    connect(chooseButton, &QPushButton::clicked, this, &CutBuildingDialog::choosePicture);
    pictureRow->addWidget(chooseButton);
    main->addLayout(pictureRow);

    auto* panes = new QHBoxLayout();
    panes->setSpacing(ui::theme::spacing::LOOSE);

    auto* sourceBox = new QGroupBox("Click the foot of the walls, left to right");
    auto* sourceLayout = new QVBoxLayout(sourceBox);
    _sourceLabel = new QLabel();
    _sourceLabel->setFixedSize(SOURCE_W, SOURCE_H);
    _sourceLabel->setCursor(Qt::CrossCursor);
    _sourceLabel->installEventFilter(this);
    sourceLayout->addWidget(_sourceLabel);
    auto* hint = new QLabel("One point at each end and one at each corner, where the wall meets the ground. "
                            "Right-click takes the last point back. Fewer than two points: the tool guesses "
                            "(drawn in blue). Shift-click the top of a wall, under the eaves, to set the wall "
                            "height (the orange line): above it is roof.");
    hint->setWordWrap(true);
    hint->setMaximumWidth(SOURCE_W);
    sourceLayout->addWidget(hint);
    auto* sourceButtons = new QHBoxLayout();
    auto* clearButton = new QPushButton("Clear Points");
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        _points.clear();
        drawSourceView();
        schedulePreview();
    });
    sourceButtons->addWidget(clearButton);
    auto* useGuessButton = new QPushButton("Edit the Guess");
    useGuessButton->setToolTip("Take the guessed points as clicked ones: right-click takes the last back, a click adds one");
    connect(useGuessButton, &QPushButton::clicked, this, [this]() {
        if (_points.size() < 2 && _guessed.size() >= 2) {
            _points = _guessed;
            drawSourceView();
        }
    });
    sourceButtons->addWidget(useGuessButton);
    sourceButtons->addStretch();
    sourceLayout->addLayout(sourceButtons);
    panes->addWidget(sourceBox);

    auto* previewBox = new QGroupBox("In the game, beside the player, and where it is cut");
    auto* previewLayout = new QVBoxLayout(previewBox);
    auto* scroll = new QScrollArea();
    scroll->setMinimumSize(440, SOURCE_H);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setStyleSheet(QString("QScrollArea { background: %1; }").arg(PANE_BACKGROUND.name()));
    _previewLabel = new QLabel("Choose a render to see it here.");
    _previewLabel->setAlignment(Qt::AlignCenter);
    _previewLabel->setWordWrap(true);
    scroll->setWidget(_previewLabel);
    scroll->setWidgetResizable(true);
    previewLayout->addWidget(scroll, 1);
    auto* zoomRow = new QHBoxLayout();
    zoomRow->addStretch();
    zoomRow->addWidget(new QLabel("Zoom:"));
    _zoomCombo = new QComboBox();
    _zoomCombo->addItems({ "1x", "2x", "3x" });
    connect(_zoomCombo, &QComboBox::currentIndexChanged, this, &CutBuildingDialog::drawPreview);
    zoomRow->addWidget(_zoomCombo);
    previewLayout->addLayout(zoomRow);
    panes->addWidget(previewBox, 1);
    main->addLayout(panes, 1);

    auto* form = new QFormLayout();
    form->setSpacing(ui::theme::spacing::NORMAL);

    auto* namesRow = new QHBoxLayout();
    _prefixEdit = new QLineEdit();
    _prefixEdit->setMaxLength(5);
    _prefixEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("[a-z0-9_]{0,5}"), _prefixEdit));
    _prefixEdit->setPlaceholderText("up to 5 letters");
    _prefixEdit->setToolTip("The pieces are named this plus 001, 002, ...");
    _prefixEdit->setMaximumWidth(130);
    namesRow->addWidget(_prefixEdit);
    namesRow->addWidget(new QLabel("Stamp name:"));
    _stampEdit = new QLineEdit();
    _stampEdit->setPlaceholderText("as it shows in Edit > Stamp Pattern > Custom");
    namesRow->addWidget(_stampEdit, 1);
    form->addRow("Piece names:", namesRow);

    _problemLabel = new QLabel();
    _problemLabel->setStyleSheet(ui::theme::styles::statusError());
    _problemLabel->setWordWrap(true);
    form->addRow(QString(), _problemLabel);

    auto* sizeRow = new QHBoxLayout();
    _sizeModeCombo = new QComboBox();
    _sizeModeCombo->addItem("Height in game pixels", "height");
    _sizeModeCombo->addItem("Scale (game pixels per render pixel)", "scale");
    sizeRow->addWidget(_sizeModeCombo);
    _heightSpin = new QSpinBox();
    _heightSpin->setRange(16, 2000);
    _heightSpin->setValue(240);
    _heightSpin->setSuffix(" px");
    _heightSpin->setToolTip("The whole building, roof to foot. The player is about 70 pixels tall.");
    sizeRow->addWidget(_heightSpin);
    _scaleSpin = new QDoubleSpinBox();
    _scaleSpin->setDecimals(3);
    _scaleSpin->setRange(0.01, 4.0);
    _scaleSpin->setSingleStep(0.01);
    _scaleSpin->setValue(0.25);
    _scaleSpin->setVisible(false);
    sizeRow->addWidget(_scaleSpin);
    sizeRow->addStretch();
    form->addRow("Size:", sizeRow);

    _colourCombo = new QComboBox();
    _colourCombo->addItem("As rendered", "none");
    _colourCombo->addItem("Dusty, like the vanilla walls", "walls");
    form->addRow("Colours:", _colourCombo);

    auto* roofRow = new QHBoxLayout();
    _roofCheck = new QCheckBox("Roof as roof tiles, above a wall height of");
    _roofCheck->setChecked(true);
    _roofCheck->setToolTip("Fallout's way: the roof is drawn over everything and hides while the player stands under it. "
                           "Each tile takes one of the few spare tile slots. Off: the roof stays part of the walls.");
    roofRow->addWidget(_roofCheck);
    _wallTopSpin = new QDoubleSpinBox();
    _wallTopSpin->setDecimals(0);
    _wallTopSpin->setRange(1, 10000);
    _wallTopSpin->setSuffix(" render px");
    _wallTopSpin->setToolTip("Height of the walls above their foot, in the render's pixels. Shift-click a wall's top to set it.");
    roofRow->addWidget(_wallTopSpin);
    _wallTopGameLabel = new QLabel();
    roofRow->addWidget(_wallTopGameLabel);
    roofRow->addStretch();
    form->addRow("Roof:", roofRow);
    main->addLayout(form);

    _infoLabel = new QLabel();
    _infoLabel->setWordWrap(true);
    _infoLabel->setStyleSheet(ui::theme::styles::statusInfo());
    main->addWidget(_infoLabel);

    auto* note = new QLabel("The pieces and the stamp go into the game with Tools > Apply New Art; then place the "
                            "building with Edit > Stamp Pattern > Custom. The foot blocks; the ground behind the "
                            "front walls does not, so add blockers if the player can walk round it.");
    note->setWordWrap(true);
    main->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    _createButton = buttons->addButton("Cut and Create", QDialogButtonBox::ActionRole);
    _createButton->setDefault(true);
    connect(_createButton, &QPushButton::clicked, this, &CutBuildingDialog::create);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main->addWidget(buttons);

    connect(_sizeModeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        const bool byHeight = _sizeModeCombo->currentData().toString() == "height";
        _heightSpin->setVisible(byHeight);
        _scaleSpin->setVisible(!byHeight);
        schedulePreview();
    });
    connect(_heightSpin, &QSpinBox::valueChanged, this, &CutBuildingDialog::schedulePreview);
    connect(_scaleSpin, &QDoubleSpinBox::valueChanged, this, &CutBuildingDialog::schedulePreview);
    connect(_colourCombo, &QComboBox::currentIndexChanged, this, &CutBuildingDialog::schedulePreview);
    connect(_roofCheck, &QCheckBox::toggled, this, [this](bool on) {
        _wallTopSpin->setEnabled(on);
        drawSourceView();
        schedulePreview();
    });
    connect(_wallTopSpin, &QDoubleSpinBox::valueChanged, this, [this]() {
        _wallTopAuto = false; // set by hand now (programmatic sets block this signal)
        drawSourceView();
        schedulePreview();
    });
    connect(_prefixEdit, &QLineEdit::textChanged, this, [this]() {
        _problemLabel->clear();
        updateCreateButton();
        schedulePreview(); // the preview checks the names
    });
    connect(_stampEdit, &QLineEdit::textChanged, this, [this]() {
        _problemLabel->clear();
        updateCreateButton();
        schedulePreview();
    });

    resize(980, 820);
}

bool CutBuildingDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == _sourceLabel && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && (mouse->modifiers() & Qt::ShiftModifier)) {
            setWallTopFrom(mouse->position().toPoint());
            return true;
        }
        if (mouse->button() == Qt::LeftButton) {
            addPoint(mouse->position().toPoint());
            return true;
        }
        if (mouse->button() == Qt::RightButton) {
            removeLastPoint();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CutBuildingDialog::choosePicture() {
    const QString path = QtDialogs::openFile(this, "Choose a Building Render",
        "Pictures (*.png *.webp *.jpg *.jpeg *.bmp *.tga *.tif *.tiff);;All Files (*.*)");
    if (!path.isEmpty()) {
        loadPicture(path);
    }
}

void CutBuildingDialog::loadPicture(const QString& path) {
    QImage image(path);
    if (image.isNull()) {
        QtDialogs::showWarning(this, "Cut Building", QString("Couldn't read this picture:\n%1").arg(path));
        return;
    }
    _picturePath = path;
    _picture = image;
    _points.clear();
    _guessed.clear();
    _wallTopAuto = true; // a new render: let the tool guess its wall height again
    _previewOk = false;
    _pictureEdit->setText(QDir::toNativeSeparators(path));
    if (_stampEdit->text().isEmpty()) {
        _stampEdit->setText(QFileInfo(path).completeBaseName());
    }
    drawSourceView();
    updateCreateButton();
    schedulePreview();
}

void CutBuildingDialog::addPoint(const QPoint& labelPos) {
    if (_picture.isNull() || _sourceScale <= 0.0 || _points.size() >= MAX_POINTS) {
        return;
    }
    const QPointF onPicture = QPointF(labelPos - _sourceOffset) / _sourceScale;
    if (onPicture.x() < 0 || onPicture.y() < 0 || onPicture.x() >= _picture.width() || onPicture.y() >= _picture.height()) {
        return;
    }
    _points.append(onPicture);
    drawSourceView();
    if (_points.size() >= 2) {
        schedulePreview();
    }
}

std::optional<double> CutBuildingDialog::footAt(double x) const {
    QList<QPointF> foot = _points.size() >= 2 ? _points : _guessed;
    if (foot.size() < 2) {
        return std::nullopt;
    }
    std::sort(foot.begin(), foot.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    const auto along = [](const QPointF& a, const QPointF& b, double at) {
        return b.x() == a.x() ? a.y() : a.y() + (b.y() - a.y()) * (at - a.x()) / (b.x() - a.x());
    };
    if (x <= foot.first().x()) {
        return along(foot[0], foot[1], x);
    }
    for (int i = 1; i < foot.size(); ++i) {
        if (x <= foot[i].x()) {
            return along(foot[i - 1], foot[i], x);
        }
    }
    return along(foot[foot.size() - 2], foot.last(), x);
}

void CutBuildingDialog::setWallTopFrom(const QPoint& labelPos) {
    if (_picture.isNull() || _sourceScale <= 0.0 || !_roofCheck->isChecked()) {
        return;
    }
    const QPointF onPicture = QPointF(labelPos - _sourceOffset) / _sourceScale;
    const std::optional<double> foot = footAt(onPicture.x());
    if (!foot || *foot - onPicture.y() < 1.0) {
        return; // no foot yet, or the click is below it
    }
    _wallTopSpin->setValue(std::round(*foot - onPicture.y())); // valueChanged marks it set and redraws
}

void CutBuildingDialog::removeLastPoint() {
    if (_points.isEmpty()) {
        return;
    }
    _points.removeLast();
    drawSourceView();
    schedulePreview();
}

void CutBuildingDialog::drawSourceView() {
    QPixmap canvas(SOURCE_W, SOURCE_H);
    canvas.fill(PANE_BACKGROUND);
    if (_picture.isNull()) {
        _sourceLabel->setPixmap(canvas);
        return;
    }
    _sourceScale = std::min({ double(SOURCE_W) / _picture.width(), double(SOURCE_H) / _picture.height(), 4.0 });
    const QSize shown(std::max(1, int(_picture.width() * _sourceScale)), std::max(1, int(_picture.height() * _sourceScale)));
    _sourceOffset = QPoint((SOURCE_W - shown.width()) / 2, (SOURCE_H - shown.height()) / 2);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawImage(QRect(_sourceOffset, shown), _picture);

    const auto drawLine = [&](QList<QPointF> points, const QColor& colour, Qt::PenStyle style) {
        std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
        QList<QPointF> onScreen;
        for (const QPointF& p : points) {
            onScreen.append(QPointF(_sourceOffset) + p * _sourceScale);
        }
        painter.setPen(QPen(Qt::black, 3));
        for (int i = 1; i < onScreen.size(); ++i) {
            painter.drawLine(onScreen[i - 1], onScreen[i]);
        }
        painter.setPen(QPen(colour, 1.5, style));
        for (int i = 1; i < onScreen.size(); ++i) {
            painter.drawLine(onScreen[i - 1], onScreen[i]);
        }
        painter.setPen(QPen(Qt::black, 1));
        painter.setBrush(colour);
        for (const QPointF& p : onScreen) {
            painter.drawEllipse(p, 3.5, 3.5);
        }
        painter.setBrush(Qt::NoBrush);
    };
    if (_points.size() >= 1) {
        drawLine(_points, CLICKED, Qt::SolidLine);
    } else if (_guessed.size() >= 2) {
        drawLine(_guessed, GUESSED, Qt::DashLine);
    }

    // the walls' top: the foot raised by the wall height, across the whole picture
    if (_roofCheck && _roofCheck->isChecked() && (!_wallTopAuto || _wallTopSpin->value() > 1) && footAt(0)) {
        const double rise = _wallTopSpin->value();
        QList<double> xs{ 0.0, double(_picture.width()) };
        for (const QPointF& p : (_points.size() >= 2 ? _points : _guessed)) {
            xs.append(p.x());
        }
        std::sort(xs.begin(), xs.end());
        QList<QPointF> top;
        for (double x : xs) {
            top.append(QPointF(_sourceOffset) + QPointF(x, *footAt(x) - rise) * _sourceScale);
        }
        painter.setPen(QPen(Qt::black, 3));
        for (int i = 1; i < top.size(); ++i) {
            painter.drawLine(top[i - 1], top[i]);
        }
        painter.setPen(QPen(WALL_TOP, 1.5, Qt::DashLine));
        for (int i = 1; i < top.size(); ++i) {
            painter.drawLine(top[i - 1], top[i]);
        }
    }
    painter.end();
    _sourceLabel->setPixmap(canvas);
}

QStringList CutBuildingDialog::buildingArguments() const {
    QStringList args{ _picturePath };
    if (_sizeModeCombo->currentData().toString() == "height") {
        args << "--height" << QString::number(_heightSpin->value());
    } else {
        args << "--scale" << QString::number(_scaleSpin->value(), 'g', 6);
    }
    args << "--colour" << _colourCombo->currentData().toString();
    if (!_prefixEdit->text().isEmpty()) {
        args << "--prefix" << _prefixEdit->text();
    }
    if (!_stampEdit->text().trimmed().isEmpty()) {
        args << "--stamp" << _stampEdit->text().trimmed();
    }
    args << "--wall-top"
         << (!_roofCheck->isChecked() ? QString("none")
                 : _wallTopAuto      ? QString("auto")
                                     : QString::number(_wallTopSpin->value(), 'f', 0));
    args << "--line"; // last: it takes every value after it
    if (_points.size() >= 2) {
        QList<QPointF> points = _points;
        std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
        for (const QPointF& p : points) {
            args << QString("%1,%2").arg(p.x(), 0, 'f', 1).arg(p.y(), 0, 'f', 1);
        }
    } else {
        args << "auto";
    }
    return args;
}

void CutBuildingDialog::schedulePreview() {
    if (!_picture.isNull()) {
        _previewTimer->start();
    }
}

void CutBuildingDialog::runPreview() {
    if (_picture.isNull()) {
        return;
    }
    if (_previewRunning) {
        _previewDirty = true;
        return;
    }
    _previewRunning = true;
    _previewDirty = false;
    _infoLabel->setStyleSheet(ui::theme::styles::statusInfo());
    _infoLabel->setText("Cutting the preview...");
    QStringList args = buildingArguments();
    const int lineAt = args.indexOf("--line");
    args.insert(lineAt, _previewPath);
    args.insert(lineAt, "--out");
    _runner->run(QStringList{ "building-preview" } + args, [this](const ModToolResult& result) {
        _previewRunning = false;
        if (_previewDirty) {
            runPreview();
            return;
        }
        showPreview(result);
    });
}

void CutBuildingDialog::showPreview(const ModToolResult& result) {
    _previewOk = result.ok;
    if (!result.ok) {
        _previewImage = QImage();
        _previewLabel->setPixmap(QPixmap());
        _previewLabel->setText("No preview.");
        _infoLabel->setStyleSheet(ui::theme::styles::statusError());
        _infoLabel->setText(result.error);
        updateCreateButton();
        return;
    }

    _guessed.clear();
    for (const auto& value : result.json.value("line").toArray()) {
        const QJsonArray xy = value.toArray();
        _guessed.append(QPointF(xy.at(0).toDouble(), xy.at(1).toDouble()));
    }
    if (_wallTopAuto && result.json.value("wall_top").isDouble()) {
        const QSignalBlocker block(_wallTopSpin); // the tool's guess, not a setting
        _wallTopSpin->setValue(std::round(result.json.value("wall_top").toDouble()));
    }
    _wallTopGameLabel->setText(result.json.value("wall_top_game").isDouble()
            ? QString("= %1 game px").arg(result.json.value("wall_top_game").toDouble(), 0, 'f', 0)
            : QString());
    drawSourceView();

    _previewImage = QImage(result.json.value("preview").toString()); // QImage: no pixmap cache to go stale
    drawPreview();

    QStringList along;
    for (const auto& stretch : result.json.value("stretches").toArray()) {
        along << stretch.toObject().value("along").toString();
    }
    QString info = QString("%1 wall pieces, %2 x %3 pixels, along %4.")
                       .arg(result.json.value("pieces").toInt())
                       .arg(result.json.value("width").toInt())
                       .arg(result.json.value("height").toInt())
                       .arg(along.join(", then "));
    const int roofTiles = result.json.value("roof_tiles").toInt();
    if (roofTiles > 0) {
        info += QString(" %1 roof tiles (%2 spare tile slots left).")
                    .arg(roofTiles)
                    .arg(result.json.value("spare_tile_slots").toInt());
    }
    for (const auto& warning : result.json.value("warnings").toArray()) {
        info += "\nNote: " + warning.toString() + ".";
    }
    _infoLabel->setStyleSheet(ui::theme::styles::statusInfo());
    _infoLabel->setText(info);

    QStringList problems;
    for (const char* key : { "name_problem", "roof_problem", "stamp_problem" }) {
        const QString problem = result.json.value(key).toString();
        if (!problem.isEmpty()) {
            problems << problem;
        }
    }
    _problemLabel->setText(problems.join(' '));
    updateCreateButton();
}

void CutBuildingDialog::drawPreview() {
    if (_previewImage.isNull()) {
        return;
    }
    const int zoom = _zoomCombo->currentIndex() + 1;
    const QImage shown = _previewImage.scaled(_previewImage.size() * zoom, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    _previewLabel->setText(QString());
    _previewLabel->setPixmap(QPixmap::fromImage(shown));
}

void CutBuildingDialog::updateCreateButton() {
    const bool ready = !_creating && !_picture.isNull() && _previewOk && !_prefixEdit->text().isEmpty()
        && !_stampEdit->text().trimmed().isEmpty() && _problemLabel->text().isEmpty();
    _createButton->setEnabled(ready);
    if (!_picture.isNull() && !ready && !_creating) {
        _createButton->setToolTip(_prefixEdit->text().isEmpty() ? QString("Still needed: the piece names")
                : _stampEdit->text().trimmed().isEmpty()        ? QString("Still needed: the stamp name")
                                                                : QString());
    } else {
        _createButton->setToolTip(QString());
    }
}

void CutBuildingDialog::create() {
    if (_creating) {
        return;
    }
    _creating = true;
    updateCreateButton();
    _infoLabel->setStyleSheet(ui::theme::styles::statusInfo());
    _infoLabel->setText("Cutting the building and making its pieces...");
    _runner->run(QStringList{ "building-create" } + buildingArguments(), [this](const ModToolResult& result) {
        _creating = false;
        if (!result.ok) {
            _infoLabel->setStyleSheet(ui::theme::styles::statusError());
            _infoLabel->setText("Not made.");
            updateCreateButton();
            QtDialogs::showWarning(this, "Cut Building", result.error);
            return;
        }
        ++_buildingsMade;
        const QJsonArray made = result.json.value("made").toArray();
        const QString first = made.isEmpty() ? QString() : made.first().toObject().value("name").toString();
        const QString last = made.isEmpty() ? QString() : made.last().toObject().value("name").toString();
        const QString stamp = QFileInfo(result.json.value("stamp").toString()).completeBaseName();
        const qsizetype roofMade = result.json.value("roof_made").toArray().size();
        const QString roofText = roofMade > 0 ? QString(", %1 roof tiles").arg(roofMade) : QString();
        _infoLabel->setStyleSheet(ui::theme::styles::statusSuccess());
        _infoLabel->setText(QString("Made %1 wall pieces (%2 to %3)%4 and the stamp \"%5\". Tools > Apply New Art puts "
                                    "them in the game; then Edit > Stamp Pattern > Custom > %5.")
                                .arg(made.size())
                                .arg(first, last, roofText, stamp));
        _prefixEdit->clear();
        _stampEdit->clear();
        updateCreateButton();
    });
}

} // namespace geck
