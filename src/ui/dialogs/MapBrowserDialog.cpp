#include "ui/dialogs/MapBrowserDialog.h"

#include <algorithm>
#include <vector>

#include <QColor>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "editor/HexagonGrid.h"
#include "resource/GameResources.h"
#include "ui/rendering/MapThumbnail.h"
#include "ui/theme/ThemeManager.h"

namespace geck {

namespace {
    constexpr int THUMBNAIL_SIZE = 128;
    // The preview is rendered once at this resolution (the renderer's own cap), then
    // scaled to fit the preview pane as it is resized. 160 is a floor so the pane can be
    // dragged small without the preview vanishing.
    constexpr int PREVIEW_RENDER_SIZE = 512;
    constexpr int PREVIEW_MIN_SIZE = 160;
    constexpr int CELL_PADDING_W = 24;
    constexpr int CELL_PADDING_H = 40;
    constexpr int PATH_ROLE = Qt::UserRole + 1;
    constexpr int RENDERED_ROLE = Qt::UserRole + 2;

    // A neutral fill shown in a cell until its real thumbnail has been rendered, so the
    // grid keeps a uniform layout instead of collapsing empty cells.
    QPixmap placeholderThumbnail() {
        QPixmap pm(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
        pm.fill(QColor(ui::theme::colors::SURFACE_DARK));
        return pm;
    }
} // namespace

MapBrowserDialog::MapBrowserDialog(resource::GameResources& resources, QWidget* parent)
    : QDialog(parent)
    , _resources(resources)
    , _hexgrid(std::make_unique<HexagonGrid>()) {
    setWindowTitle("Open Map");
    resize(860, 560);

    _search = new QLineEdit(this);
    _search->setPlaceholderText("Filter maps…");
    _search->setClearButtonEnabled(true);

    _grid = new QListWidget(this);
    _grid->setViewMode(QListView::IconMode);
    _grid->setIconSize(QSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
    _grid->setGridSize(QSize(THUMBNAIL_SIZE + CELL_PADDING_W, THUMBNAIL_SIZE + CELL_PADDING_H));
    _grid->setResizeMode(QListView::Adjust);
    _grid->setMovement(QListView::Static);
    _grid->setUniformItemSizes(true);
    _grid->setWordWrap(true);

    _previewImage = new QLabel(this);
    _previewImage->setAlignment(Qt::AlignCenter);
    _previewImage->setMinimumSize(PREVIEW_MIN_SIZE, PREVIEW_MIN_SIZE);
    // Ignore the pixmap's own size hint so the label fills whatever the splitter gives it
    // (and the scaled pixmap never feeds back into the layout); rescalePreview fits the
    // image to that size on every resize.
    _previewImage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    _previewImage->installEventFilter(this);
    _previewName = new QLabel(this);
    _previewName->setAlignment(Qt::AlignCenter);
    _previewName->setWordWrap(true);

    auto* previewPanel = new QWidget(this);
    auto* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->addWidget(_previewImage, 1);
    previewLayout->addWidget(_previewName);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(_grid);
    splitter->addWidget(previewPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Close, this);
    _openButton = buttons->button(QDialogButtonBox::Open);
    _openButton->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(_search);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);

    // Drives lazy thumbnail rendering: each timeout renders one visible cell, then the
    // event loop runs before the next, so scrolling/typing stay responsive. Stops itself
    // once every visible cell is rendered; scroll/filter restart it for newly-shown cells.
    _thumbnailTimer = new QTimer(this);
    _thumbnailTimer->setInterval(0);
    connect(_thumbnailTimer, &QTimer::timeout, this, &MapBrowserDialog::renderNextVisibleThumbnail);

    connect(_search, &QLineEdit::textChanged, this, &MapBrowserDialog::onFilterChanged);
    connect(_grid, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem* current, QListWidgetItem*) { onCurrentItemChanged(current); });
    connect(_grid, &QListWidget::itemActivated, this, &MapBrowserDialog::onItemActivated);
    connect(_grid->verticalScrollBar(), &QScrollBar::valueChanged, this,
        [this] { _thumbnailTimer->start(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &MapBrowserDialog::acceptCurrent);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populate();
}

MapBrowserDialog::~MapBrowserDialog() = default;

void MapBrowserDialog::populate() {
    _grid->clear();

    std::vector<QString> mapPaths;
    for (const auto& path : _resources.files().list("*.map")) {
        mapPaths.push_back(QString::fromStdString(path.generic_string()));
    }
    std::ranges::sort(mapPaths,
        [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });

    const QPixmap placeholder = placeholderThumbnail();
    for (const QString& path : mapPaths) {
        auto* item = new QListWidgetItem(QIcon(placeholder), QFileInfo(path).completeBaseName(), _grid);
        item->setData(PATH_ROLE, path);
        item->setData(RENDERED_ROLE, false);
        item->setToolTip(path);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    }
}

QListWidgetItem* MapBrowserDialog::nextUnrenderedVisibleItem() const {
    const QRect viewport = _grid->viewport()->rect();
    for (int i = 0; i < _grid->count(); ++i) {
        QListWidgetItem* item = _grid->item(i);
        if (item->isHidden() || item->data(RENDERED_ROLE).toBool()) {
            continue;
        }
        if (viewport.intersects(_grid->visualItemRect(item))) {
            return item;
        }
    }
    return nullptr;
}

void MapBrowserDialog::renderNextVisibleThumbnail() {
    QListWidgetItem* item = nextUnrenderedVisibleItem();
    if (item == nullptr) {
        _thumbnailTimer->stop();
        return;
    }

    const QString path = item->data(PATH_ROLE).toString();
    const QPixmap thumbnail = MapThumbnail::forMap(path, _resources, *_hexgrid, THUMBNAIL_SIZE);
    // Mark rendered even on failure, so an unreadable map isn't retried every tick.
    item->setData(RENDERED_ROLE, true);
    if (!thumbnail.isNull()) {
        item->setIcon(QIcon(thumbnail));
    }
}

void MapBrowserDialog::onFilterChanged(const QString& text) {
    const QString needle = text.trimmed();
    for (int i = 0; i < _grid->count(); ++i) {
        QListWidgetItem* item = _grid->item(i);
        const bool match = needle.isEmpty()
            || item->text().contains(needle, Qt::CaseInsensitive)
            || item->data(PATH_ROLE).toString().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
    _thumbnailTimer->start(); // render any cells the filter just revealed
}

void MapBrowserDialog::onCurrentItemChanged(const QListWidgetItem* current) {
    _openButton->setEnabled(current != nullptr);
    updatePreview(current);
}

void MapBrowserDialog::updatePreview(const QListWidgetItem* item) {
    _previewSource = QPixmap();
    if (item == nullptr) {
        _previewImage->clear();
        _previewName->clear();
        return;
    }

    const QString path = item->data(PATH_ROLE).toString();
    _previewName->setText(path);
    _previewImage->setText("Rendering…");
    // Defer the heavy render so selection stays snappy; bail if the selection has since
    // moved on (the cache makes a later re-selection of the same map instant).
    QTimer::singleShot(0, this, [this, path] {
        if (const QListWidgetItem* current = _grid->currentItem();
            current == nullptr || current->data(PATH_ROLE).toString() != path) {
            return;
        }
        _previewSource = MapThumbnail::forMap(path, _resources, *_hexgrid, PREVIEW_RENDER_SIZE);
        if (_previewSource.isNull()) {
            _previewImage->setText("No preview");
        } else {
            rescalePreview();
        }
    });
}

void MapBrowserDialog::rescalePreview() {
    if (_previewSource.isNull()) {
        return;
    }
    _previewImage->setPixmap(_previewSource.scaled(
        _previewImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool MapBrowserDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == _previewImage && event->type() == QEvent::Resize) {
        rescalePreview();
    }
    return QDialog::eventFilter(watched, event);
}

void MapBrowserDialog::onItemActivated(const QListWidgetItem* item) {
    if (item != nullptr) {
        _selectedPath = item->data(PATH_ROLE).toString();
        accept();
    }
}

void MapBrowserDialog::acceptCurrent() {
    if (const QListWidgetItem* item = _grid->currentItem()) {
        _selectedPath = item->data(PATH_ROLE).toString();
        accept();
    }
}

void MapBrowserDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    _thumbnailTimer->start(); // viewport geometry is valid now, so visible cells resolve
}

} // namespace geck
