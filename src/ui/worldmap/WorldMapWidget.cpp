#include "ui/worldmap/WorldMapWidget.h"

#include "editor/worldmap/WorldMapScene.h"
#include "ui/theme/ThemeManager.h"
#include "ui/worldmap/WorldMapView.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

namespace geck {

namespace {

    // Wide enough for the longest shipped area name plus its "known at start" marker.
    constexpr int AREA_LIST_WIDTH = 220;

    constexpr int AREA_INDEX_ROLE = Qt::UserRole;

} // namespace

WorldMapWidget::WorldMapWidget(resource::GameResources& resources, QWidget* parent)
    : QWidget(parent)
    , _resources(resources) {
    setupUi();
}

WorldMapWidget::~WorldMapWidget() = default;

bool WorldMapWidget::hasScene() const {
    return _scene != nullptr;
}

void WorldMapWidget::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ui::theme::spacing::MARGIN_NORMAL, ui::theme::spacing::MARGIN_NORMAL,
        ui::theme::spacing::MARGIN_NORMAL, ui::theme::spacing::MARGIN_NORMAL);
    layout->setSpacing(ui::theme::spacing::NORMAL);

    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(ui::theme::spacing::NORMAL);

    auto* markers = new QCheckBox(tr("Markers"), this);
    markers->setChecked(true);
    markers->setToolTip(tr("Draw the area circles the game blends over the map"));
    connect(markers, &QCheckBox::toggled, this, [this](bool on) { _view->setMarkersVisible(on); });
    toolbar->addWidget(markers);

    auto* labels = new QCheckBox(tr("Labels"), this);
    labels->setChecked(true);
    labels->setToolTip(tr("Draw the area names under their circles"));
    connect(labels, &QCheckBox::toggled, this, [this](bool on) { _view->setLabelsVisible(on); });
    toolbar->addWidget(labels);

    auto* fit = new QToolButton(this);
    fit->setText(tr("Fit"));
    fit->setToolTip(tr("Scale the whole world map to the window"));
    connect(fit, &QToolButton::clicked, this, [this] { _view->zoomToFit(); });
    toolbar->addWidget(fit);

    auto* actual = new QToolButton(this);
    actual->setText(tr("1:1"));
    actual->setToolTip(tr("Show the map at its native size, as the game draws it"));
    connect(actual, &QToolButton::clicked, this, [this] { _view->zoomToActualSize(); });
    toolbar->addWidget(actual);

    // Missing art is the scene's own diagnosis; without it on screen the user just sees black
    // tiles, or markers that cannot be clicked, with nothing to explain why.
    _warning = new QLabel(this);
    _warning->setStyleSheet(ui::theme::styles::statusWarning());
    _warning->setVisible(false);
    toolbar->addWidget(_warning);

    toolbar->addStretch();

    // Two labels, not one: the summary is about the map and the readout about the pointer, and a
    // single label meant the area count vanished on the first mouse move.
    _summary = new QLabel(this);
    _summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    toolbar->addWidget(_summary);

    _status = new QLabel(this);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _status->setMinimumWidth(AREA_LIST_WIDTH);
    _status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    toolbar->addWidget(_status);
    layout->addLayout(toolbar);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left: the map (or, when there is no worldmap data, an explanation in its place).
    _canvasStack = new QStackedWidget(splitter);
    _view = new WorldMapView(_canvasStack);
    _canvasStack->addWidget(_view);

    _placeholder = new QLabel(_canvasStack);
    _placeholder->setAlignment(Qt::AlignCenter);
    _placeholder->setWordWrap(true);
    _placeholder->setStyleSheet(ui::theme::styles::statusError());
    _canvasStack->addWidget(_placeholder);
    splitter->addWidget(_canvasStack);

    connect(_view, &WorldMapView::areaSelected, this, &WorldMapWidget::onAreaSelected);
    connect(_view, &WorldMapView::areaActivated, this, &WorldMapWidget::onAreaActivated);
    connect(_view, &WorldMapView::hovered, this, &WorldMapWidget::onHovered);

    // Right: the area list, filtered.
    auto* side = new QWidget(splitter);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(ui::theme::spacing::TIGHT);

    _filter = new QLineEdit(side);
    _filter->setPlaceholderText(tr("Filter areas..."));
    _filter->setClearButtonEnabled(true);
    connect(_filter, &QLineEdit::textChanged, this, &WorldMapWidget::onFilterChanged);
    sideLayout->addWidget(_filter);

    _areaList = new QListWidget(side);
    _areaList->setMinimumWidth(AREA_LIST_WIDTH);
    connect(_areaList, &QListWidget::currentItemChanged, this, [this](const QListWidgetItem* item, QListWidgetItem*) {
        if (item != nullptr) {
            _view->revealArea(item->data(AREA_INDEX_ROLE).toInt());
        }
    });
    sideLayout->addWidget(_areaList, 1);

    _openMapButton = new QToolButton(side);
    _openMapButton->setText(tr("Open Map"));
    _openMapButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    _openMapButton->setEnabled(false);
    _openMapButton->setToolTip(tr("Open the selected area's first map in the editor"));
    connect(_openMapButton, &QToolButton::clicked, this, [this] {
        if (!_selectedMapFile.empty()) {
            Q_EMIT openMapRequested(QString::fromStdString(_selectedMapFile));
        }
    });
    sideLayout->addWidget(_openMapButton);

    splitter->addWidget(side);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    layout->addWidget(splitter, 1);

    // The stack starts on the placeholder until a load succeeds.
    _canvasStack->setCurrentWidget(_placeholder);
    _placeholder->setText(tr("No world map loaded."));
}

bool WorldMapWidget::reload() {
    _scene = worldmap::WorldMapScene::load(_resources);
    _view->setScene(_scene);
    _selectedMapFile.clear();
    _openMapButton->setEnabled(false);
    _status->clear();
    _warning->setVisible(false);

    if (!_scene) {
        _placeholder->setText(tr("The world map needs city.txt, worldmap.txt and color.pal from the "
                                 "game data. Add a Fallout 2 data folder or master.dat in "
                                 "Preferences, then try again."));
        _canvasStack->setCurrentWidget(_placeholder);
        _areaList->clear();
        _summary->clear();
        return false;
    }

    _canvasStack->setCurrentWidget(_view);
    refreshAreaList();
    showMissingArt();
    _summary->setText(tr("%1 areas").arg(_scene->areas().size()));
    return true;
}

void WorldMapWidget::showMissingArt() {
    const std::vector<std::string>& missing = _scene->missingArt();
    if (missing.empty()) {
        return;
    }

    spdlog::warn("World map: {} piece(s) of art are missing", missing.size());

    QStringList details;
    for (const std::string& entry : missing) {
        details << QString::fromStdString(entry);
    }
    _warning->setText(tr("%n art file(s) missing", "", static_cast<int>(missing.size())));
    _warning->setToolTip(details.join(QLatin1Char('\n')));
    _warning->setVisible(true);
}

void WorldMapWidget::refreshAreaList() {
    // Filling the list must not move the map: QListWidget makes the first inserted row current, and
    // that would fire currentItemChanged and scroll the view to an area the user never picked.
    const QSignalBlocker blocker(_areaList);

    _areaList->clear();
    if (!_scene) {
        return;
    }

    const QString filter = _filter->text().trimmed();
    for (const worldmap::AreaMarker& area : _scene->areas()) {
        const QString label = QString::fromStdString(area.label());
        const QString internal = QString::fromStdString(area.name);
        if (!filter.isEmpty()
            && !label.contains(filter, Qt::CaseInsensitive)
            && !internal.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }

        auto* item = new QListWidgetItem(label, _areaList);
        item->setData(AREA_INDEX_ROLE, area.index);
        item->setToolTip(internal);
        if (!area.knownAtStart) {
            // Areas the player has to find are dimmed, so the starting world reads at a glance.
            item->setForeground(QColor(ui::theme::colors::TEXT_SECONDARY));
        }
    }
}

void WorldMapWidget::onFilterChanged(const QString&) {
    refreshAreaList();
}

void WorldMapWidget::onAreaSelected(const worldmap::AreaMarker* area) {
    _selectedMapFile = area != nullptr && !area->mapFiles.empty() ? area->mapFiles.front() : std::string();
    _openMapButton->setEnabled(!_selectedMapFile.empty());

    if (area == nullptr) {
        return;
    }

    // Mirror the click into the list without bouncing back into the view.
    const QSignalBlocker blocker(_areaList);
    for (int row = 0; row < _areaList->count(); ++row) {
        if (_areaList->item(row)->data(AREA_INDEX_ROLE).toInt() == area->index) {
            _areaList->setCurrentRow(row);
            break;
        }
    }
}

void WorldMapWidget::onAreaActivated(const worldmap::AreaMarker* area) {
    if (area != nullptr && !area->mapFiles.empty()) {
        Q_EMIT openMapRequested(QString::fromStdString(area->mapFiles.front()));
    }
}

void WorldMapWidget::onHovered(int worldX, int worldY, const worldmap::AreaMarker* area) {
    QString text = QStringLiteral("%1, %2").arg(worldX).arg(worldY);
    if (_scene) {
        const std::string terrain = _scene->terrainAt(worldX, worldY);
        if (!terrain.empty()) {
            text += QStringLiteral(" · %1").arg(QString::fromStdString(terrain));
        }
    }
    if (area != nullptr) {
        text += QStringLiteral(" · %1").arg(QString::fromStdString(area->label()));
    }
    _status->setText(text);
}

} // namespace geck
