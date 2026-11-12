#include "AddSceneryDialog.h"

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
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace geck {

using namespace ui::constants;

namespace {

    constexpr int SOURCE_W = 340;
    constexpr int SOURCE_H = 300;
    constexpr int PREVIEW_DELAY_MS = 450;

    const QColor PANE_BACKGROUND(40, 36, 30);
    const QColor STAND_MARK(225, 200, 90);

} // namespace

AddSceneryDialog::AddSceneryDialog(ModToolsRunner* runner, QWidget* parent)
    : QDialog(parent)
    , _runner(runner)
    , _previewPath(QDir(QDir::tempPath()).filePath(
          QString("gecko-add-scenery-%1.png").arg(QCoreApplication::applicationPid()))) {
    setWindowTitle("Add Scenery from Picture");
    setModal(true);
    setupUI();

    _previewTimer = new QTimer(this);
    _previewTimer->setSingleShot(true);
    _previewTimer->setInterval(PREVIEW_DELAY_MS);
    connect(_previewTimer, &QTimer::timeout, this, &AddSceneryDialog::runPreview);

    drawSourceView();
    updateCreateButton();
}

void AddSceneryDialog::setupUI() {
    auto* main = new QVBoxLayout(this);
    main->setContentsMargins(SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE);
    main->setSpacing(SPACING_LOOSE);

    // picture
    auto* pictureRow = new QHBoxLayout();
    pictureRow->addWidget(new QLabel("Picture:"));
    _pictureEdit = new QLineEdit();
    _pictureEdit->setReadOnly(true);
    _pictureEdit->setPlaceholderText("A render or photo: transparent background, or plain white");
    pictureRow->addWidget(_pictureEdit, 1);
    auto* chooseButton = new QPushButton("Choose...");
    connect(chooseButton, &QPushButton::clicked, this, &AddSceneryDialog::choosePicture);
    pictureRow->addWidget(chooseButton);
    main->addLayout(pictureRow);

    // the two panes
    auto* panes = new QHBoxLayout();
    panes->setSpacing(SPACING_LOOSE);

    auto* sourceBox = new QGroupBox("Your picture: click the point that stands on the hex");
    auto* sourceLayout = new QVBoxLayout(sourceBox);
    _sourceLabel = new QLabel();
    _sourceLabel->setFixedSize(SOURCE_W, SOURCE_H);
    _sourceLabel->setAlignment(Qt::AlignCenter);
    _sourceLabel->setCursor(Qt::CrossCursor);
    _sourceLabel->installEventFilter(this);
    sourceLayout->addWidget(_sourceLabel);
    panes->addWidget(sourceBox);

    auto* previewBox = new QGroupBox("In the game, beside the player");
    auto* previewLayout = new QVBoxLayout(previewBox);
    auto* scroll = new QScrollArea();
    scroll->setMinimumSize(420, SOURCE_H);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setStyleSheet(QString("QScrollArea { background: %1; }").arg(PANE_BACKGROUND.name()));
    _previewLabel = new QLabel("Choose a picture to see it here.");
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
    _zoomCombo->setCurrentIndex(1);
    connect(_zoomCombo, &QComboBox::currentIndexChanged, this, &AddSceneryDialog::drawPreview);
    zoomRow->addWidget(_zoomCombo);
    previewLayout->addLayout(zoomRow);
    panes->addWidget(previewBox, 1);
    main->addLayout(panes, 1);

    // the piece
    auto* form = new QFormLayout();
    form->setSpacing(SPACING_NORMAL);

    auto* nameRow = new QHBoxLayout();
    _nameEdit = new QLineEdit();
    _nameEdit->setMaxLength(8);
    _nameEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("[a-z0-9_]{0,8}"), _nameEdit));
    _nameEdit->setPlaceholderText("up to 8 letters, e.g. saloonsg");
    _nameEdit->setMaximumWidth(160);
    nameRow->addWidget(_nameEdit);
    _nameProblemLabel = new QLabel();
    _nameProblemLabel->setStyleSheet(ui::theme::styles::statusError());
    _nameProblemLabel->setWordWrap(true);
    nameRow->addWidget(_nameProblemLabel, 1);
    form->addRow("File name:", nameRow);

    _titleEdit = new QLineEdit();
    _titleEdit->setPlaceholderText("What the game calls it, e.g. Saloon Sign");
    form->addRow("Name in the game:", _titleEdit);

    _textEdit = new QLineEdit();
    _textEdit->setPlaceholderText("What the player reads on looking at it");
    form->addRow("Examine text:", _textEdit);

    _kindCombo = new QComboBox();
    _kindCombo->addItem("Sign or prop: stands on its hex and blocks it", "prop");
    _kindCombo->addItem("Vehicle: blocks its hex, stands on its middle", "vehicle");
    _kindCombo->addItem("Flat on the ground: walked over, drawn under everything", "flat");
    form->addRow("Kind:", _kindCombo);

    auto* sizeRow = new QHBoxLayout();
    _sizeModeCombo = new QComboBox();
    _sizeModeCombo->addItem("Height in game pixels", "height");
    _sizeModeCombo->addItem("Scale (game pixels per picture pixel)", "scale");
    sizeRow->addWidget(_sizeModeCombo);
    _heightSpin = new QSpinBox();
    _heightSpin->setRange(8, 1500);
    _heightSpin->setValue(100);
    _heightSpin->setSuffix(" px");
    _heightSpin->setToolTip("The player is about 70 pixels tall.");
    sizeRow->addWidget(_heightSpin);
    _scaleSpin = new QDoubleSpinBox();
    _scaleSpin->setDecimals(3);
    _scaleSpin->setRange(0.01, 4.0);
    _scaleSpin->setSingleStep(0.01);
    _scaleSpin->setValue(0.25);
    _scaleSpin->setToolTip("Use one scale for every view of the same model, so they match in size.");
    _scaleSpin->setVisible(false);
    sizeRow->addWidget(_scaleSpin);
    sizeRow->addStretch();
    form->addRow("Size:", sizeRow);

    _standCombo = new QComboBox();
    _standCombo->addItem(defaultStandLabel(), "default");
    _standCombo->addItem("Bottom middle", "foot");
    _standCombo->addItem("Middle of the piece", "centre");
    _standCombo->addItem("The point I clicked", "point");
    form->addRow("Stands on:", _standCombo);

    _colourCombo = new QComboBox();
    _colourCombo->addItem("As drawn (lit and shaded only)", "none");
    _colourCombo->addItem("Dusty, like the vanilla signs", "signs");
    _colourCombo->addItem("Dusty, like the vanilla cars", "vehicles");
    form->addRow("Colours:", _colourCombo);

    auto* shadowRow = new QHBoxLayout();
    _shadowCheck = new QCheckBox("Also make a ground shadow (a second, flat piece)");
    shadowRow->addWidget(_shadowCheck);
    _shadowNameEdit = new QLineEdit();
    _shadowNameEdit->setMaxLength(8);
    _shadowNameEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("[a-z0-9_]{0,8}"), _shadowNameEdit));
    _shadowNameEdit->setPlaceholderText("its file name (blank: automatic)");
    _shadowNameEdit->setMaximumWidth(200);
    _shadowNameEdit->setEnabled(false);
    shadowRow->addWidget(_shadowNameEdit);
    shadowRow->addStretch();
    form->addRow("Shadow:", shadowRow);
    main->addLayout(form);

    _infoLabel = new QLabel();
    _infoLabel->setWordWrap(true);
    _infoLabel->setStyleSheet(ui::theme::styles::statusInfo());
    main->addWidget(_infoLabel);

    auto* note = new QLabel("A new piece goes into the Scenery palette with Tools > Apply New Art, which "
                            "saves the map, deploys the mod and opens gecko again.");
    note->setWordWrap(true);
    main->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    _createButton = buttons->addButton("Create Piece", QDialogButtonBox::ActionRole);
    _createButton->setDefault(true);
    connect(_createButton, &QPushButton::clicked, this, &AddSceneryDialog::create);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main->addWidget(buttons);

    // Anything that changes the art redraws the preview; the names only re-check.
    connect(_kindCombo, &QComboBox::currentIndexChanged, this, [this]() {
        _standCombo->setItemText(0, defaultStandLabel());
        schedulePreview();
    });
    connect(_sizeModeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        const bool byHeight = _sizeModeCombo->currentData().toString() == "height";
        _heightSpin->setVisible(byHeight);
        _scaleSpin->setVisible(!byHeight);
        schedulePreview();
    });
    connect(_heightSpin, &QSpinBox::valueChanged, this, &AddSceneryDialog::schedulePreview);
    connect(_scaleSpin, &QDoubleSpinBox::valueChanged, this, &AddSceneryDialog::schedulePreview);
    connect(_standCombo, &QComboBox::currentIndexChanged, this, [this]() {
        drawSourceView();
        schedulePreview();
    });
    connect(_colourCombo, &QComboBox::currentIndexChanged, this, &AddSceneryDialog::schedulePreview);
    connect(_shadowCheck, &QCheckBox::toggled, this, [this](bool on) {
        _shadowNameEdit->setEnabled(on);
        schedulePreview();
    });
    connect(_nameEdit, &QLineEdit::textChanged, this, [this]() {
        _nameProblemLabel->clear();
        updateCreateButton();
        schedulePreview(); // the preview also checks the name
    });
    connect(_titleEdit, &QLineEdit::textChanged, this, &AddSceneryDialog::updateCreateButton);
    connect(_textEdit, &QLineEdit::textChanged, this, &AddSceneryDialog::updateCreateButton);

    resize(900, 760);
}

QString AddSceneryDialog::defaultStandLabel() const {
    const QString kind = _kindCombo ? _kindCombo->currentData().toString() : QString("prop");
    return kind == "prop" ? QString("Bottom middle (for this kind)") : QString("Middle of the piece (for this kind)");
}

bool AddSceneryDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == _sourceLabel && event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            pickStandPoint(mouse->position().toPoint());
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void AddSceneryDialog::choosePicture() {
    const QString path = QtDialogs::openFile(this, "Choose a Picture",
        "Pictures (*.png *.webp *.jpg *.jpeg *.bmp *.tga *.tif *.tiff);;All Files (*.*)");
    if (!path.isEmpty()) {
        loadPicture(path);
    }
}

void AddSceneryDialog::loadPicture(const QString& path) {
    QImage image(path);
    if (image.isNull()) {
        QtDialogs::showWarning(this, "Add Scenery", QString("Couldn't read this picture:\n%1").arg(path));
        return;
    }
    _picturePath = path;
    _picture = image;
    _standPoint.reset();
    if (_standCombo->currentData().toString() == "point") {
        _standCombo->setCurrentIndex(0);
    }
    _pictureEdit->setText(QDir::toNativeSeparators(path));
    _previewOk = false;
    drawSourceView();
    updateCreateButton();

    // Offer a file name from the picture's own name when none is typed yet.
    if (_nameEdit->text().isEmpty()) {
        const QString stem = QFileInfo(path).completeBaseName();
        _runner->run({ "suggest-name", stem }, [this](const ModToolResult& result) {
            if (result.ok && _nameEdit->text().isEmpty()) {
                _nameEdit->setText(result.json.value("name").toString());
            }
        });
    }
    schedulePreview();
}

void AddSceneryDialog::pickStandPoint(const QPoint& labelPos) {
    if (_picture.isNull() || _sourceScale <= 0.0) {
        return;
    }
    const QPointF onPicture = QPointF(labelPos - _sourceOffset) / _sourceScale;
    if (onPicture.x() < 0 || onPicture.y() < 0 || onPicture.x() >= _picture.width() || onPicture.y() >= _picture.height()) {
        return;
    }
    _standPoint = onPicture;
    const int pointIndex = _standCombo->findData("point");
    if (_standCombo->currentIndex() != pointIndex) {
        _standCombo->setCurrentIndex(pointIndex); // redraws and schedules through its signal
    } else {
        drawSourceView();
        schedulePreview();
    }
}

void AddSceneryDialog::drawSourceView() {
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
    painter.drawImage(QRect(_sourceOffset, shown), _picture);
    if (_standPoint && _standCombo->currentData().toString() == "point") {
        const QPointF at = QPointF(_sourceOffset) + *_standPoint * _sourceScale;
        painter.setPen(QPen(Qt::black, 3));
        painter.drawLine(at - QPointF(9, 0), at + QPointF(9, 0));
        painter.drawLine(at - QPointF(0, 9), at + QPointF(0, 9));
        painter.setPen(QPen(STAND_MARK, 1));
        painter.drawLine(at - QPointF(8, 0), at + QPointF(8, 0));
        painter.drawLine(at - QPointF(0, 8), at + QPointF(0, 8));
    }
    painter.end();
    _sourceLabel->setPixmap(canvas);
}

QStringList AddSceneryDialog::pieceArguments() const {
    QStringList args{ _picturePath, "--kind", _kindCombo->currentData().toString() };
    if (!_nameEdit->text().isEmpty()) {
        args << "--name" << _nameEdit->text();
    }
    if (_sizeModeCombo->currentData().toString() == "height") {
        args << "--height" << QString::number(_heightSpin->value());
    } else {
        args << "--scale" << QString::number(_scaleSpin->value(), 'g', 6);
    }
    const QString stand = _standCombo->currentData().toString();
    if (stand == "foot" || stand == "centre") {
        args << "--stand" << stand;
    } else if (stand == "point" && _standPoint) {
        args << "--stand" << QString("%1,%2").arg(_standPoint->x(), 0, 'f', 1).arg(_standPoint->y(), 0, 'f', 1);
    }
    args << "--colour" << _colourCombo->currentData().toString();
    if (_shadowCheck->isChecked()) {
        args << "--shadow";
        if (!_shadowNameEdit->text().isEmpty()) {
            args << "--shadow-name" << _shadowNameEdit->text();
        }
    }
    return args;
}

void AddSceneryDialog::schedulePreview() {
    if (_picture.isNull()) {
        return;
    }
    _previewTimer->start();
}

void AddSceneryDialog::runPreview() {
    if (_picture.isNull()) {
        return;
    }
    if (_previewRunning) {
        _previewDirty = true; // run again with the newest settings when this one ends
        return;
    }
    _previewRunning = true;
    _previewDirty = false;
    _infoLabel->setText("Drawing the preview...");
    _runner->run(QStringList{ "preview" } + pieceArguments() + QStringList{ "--out", _previewPath },
        [this](const ModToolResult& result) {
            _previewRunning = false;
            if (_previewDirty) {
                runPreview();
                return;
            }
            showPreview(result);
        });
}

void AddSceneryDialog::showPreview(const ModToolResult& result) {
    _previewOk = result.ok;
    if (!result.ok) {
        _previewImage = QImage();
        _previewLabel->setPixmap(QPixmap());
        _previewLabel->setText("No preview.");
        _infoLabel->setText(result.error);
        _infoLabel->setStyleSheet(ui::theme::styles::statusError());
        updateCreateButton();
        return;
    }
    _infoLabel->setStyleSheet(ui::theme::styles::statusInfo());
    _previewImage = QImage(result.json.value("preview").toString()); // QImage: no pixmap cache to go stale
    drawPreview();

    const int width = result.json.value("width").toInt();
    const int height = result.json.value("height").toInt();
    _infoLabel->setText(QString("%1 x %2 pixels, about %3 hexes wide. The yellow hex is the one it stands on.")
                            .arg(width)
                            .arg(height)
                            .arg(result.json.value("hexes_wide").toDouble()));
    const QString problem = result.json.value("name_problem").toString();
    _nameProblemLabel->setText(problem);
    updateCreateButton();
}

void AddSceneryDialog::drawPreview() {
    if (_previewImage.isNull()) {
        return;
    }
    const int zoom = _zoomCombo->currentIndex() + 1;
    const QImage shown = _previewImage.scaled(_previewImage.size() * zoom, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    _previewLabel->setText(QString());
    _previewLabel->setPixmap(QPixmap::fromImage(shown));
}

void AddSceneryDialog::updateCreateButton() {
    const bool ready = !_creating && !_picture.isNull() && _previewOk && !_nameEdit->text().isEmpty()
        && _nameProblemLabel->text().isEmpty() && !_titleEdit->text().trimmed().isEmpty()
        && !_textEdit->text().trimmed().isEmpty();
    _createButton->setEnabled(ready);
    if (!_picture.isNull() && !ready && !_creating) {
        QString missing;
        if (_nameEdit->text().isEmpty()) {
            missing = "a file name";
        } else if (_titleEdit->text().trimmed().isEmpty()) {
            missing = "the name in the game";
        } else if (_textEdit->text().trimmed().isEmpty()) {
            missing = "the examine text";
        }
        _createButton->setToolTip(missing.isEmpty() ? QString() : "Still needed: " + missing);
    } else {
        _createButton->setToolTip(QString());
    }
}

void AddSceneryDialog::create() {
    if (_creating) {
        return;
    }
    _creating = true;
    updateCreateButton();
    _infoLabel->setStyleSheet(ui::theme::styles::statusInfo());
    _infoLabel->setText("Making the piece...");
    const QString name = _nameEdit->text();
    _runner->run(QStringList{ "create" } + pieceArguments()
            + QStringList{ "--title", _titleEdit->text().trimmed(), "--text", _textEdit->text().trimmed() },
        [this, name](const ModToolResult& result) {
            _creating = false;
            if (!result.ok) {
                _infoLabel->setStyleSheet(ui::theme::styles::statusError());
                _infoLabel->setText("Not made.");
                updateCreateButton();
                QtDialogs::showWarning(this, "Add Scenery", result.error);
                return;
            }
            ++_piecesMade;
            const QString pid = result.json.value("pid").toString();
            QString made = QString("Made %1 (%2).").arg(name, pid);
            const QJsonObject shadow = result.json.value("shadow").toObject();
            if (!shadow.isEmpty()) {
                made += QString(" Its shadow is %1 (%2): place it on the hex just north of the piece.")
                            .arg(shadow.value("name").toString(), shadow.value("pid").toString());
            }
            Q_EMIT pieceCreated(name, pid);

            // Ready for the next piece: same picture and settings, new names.
            _nameEdit->clear();
            _shadowNameEdit->clear();
            _titleEdit->clear();
            _textEdit->clear();
            _infoLabel->setStyleSheet(ui::theme::styles::statusSuccess());
            _infoLabel->setText(made + " Tools > Apply New Art puts it in the palette.");
            updateCreateButton();
        });
}

} // namespace geck
