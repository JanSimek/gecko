#include "ui/dialogs/PatternBrowserDialog.h"

#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>

#include "editor/HexagonGrid.h"
#include "pattern/PatternLibrary.h"
#include "pattern/PatternSerializer.h"
#include "pattern/PatternThumbnail.h"

namespace geck {

namespace {
    constexpr int THUMBNAIL_SIZE = 96;
    constexpr int PATH_ROLE = Qt::UserRole + 1;

    std::optional<pattern::Pattern> loadPattern(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        return pattern::PatternSerializer::deserialize(file.readAll());
    }
} // namespace

PatternBrowserDialog::PatternBrowserDialog(resource::GameResources& resources, QWidget* parent)
    : QDialog(parent)
    , _resources(resources)
    , _hexgrid(std::make_unique<HexagonGrid>()) {
    setWindowTitle("Pattern Library");
    resize(720, 480);

    const QString root = pattern::PatternLibrary::rootDir();

    _folderModel = new QFileSystemModel(this);
    _folderModel->setRootPath(root);
    _folderModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

    _folderTree = new QTreeView(this);
    _folderTree->setModel(_folderModel);
    _folderTree->setRootIndex(_folderModel->index(root));
    _folderTree->setHeaderHidden(true);
    for (int column = 1; column < _folderModel->columnCount(); ++column) {
        _folderTree->hideColumn(column); // show names only, not size/type/date
    }

    _grid = new QListWidget(this);
    _grid->setViewMode(QListView::IconMode);
    _grid->setIconSize(QSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
    _grid->setGridSize(QSize(THUMBNAIL_SIZE + 24, THUMBNAIL_SIZE + 36));
    _grid->setResizeMode(QListView::Adjust);
    _grid->setMovement(QListView::Static);
    _grid->setWordWrap(true);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(_folderTree);
    splitter->addWidget(_grid);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    auto* importButton = new QPushButton("Import…", this);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addButton(importButton, QDialogButtonBox::ActionRole);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);

    connect(_folderTree->selectionModel(), &QItemSelectionModel::currentChanged,
        this, [this](const QModelIndex& current, const QModelIndex&) { onFolderSelected(current); });
    connect(_grid, &QListWidget::itemActivated, this, &PatternBrowserDialog::onPatternActivated);
    connect(importButton, &QPushButton::clicked, this, &PatternBrowserDialog::onImport);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populateGrid(root);
}

PatternBrowserDialog::~PatternBrowserDialog() = default;

void PatternBrowserDialog::onFolderSelected(const QModelIndex& index) {
    if (index.isValid()) {
        populateGrid(_folderModel->filePath(index));
    }
}

void PatternBrowserDialog::populateGrid(const QString& folder) {
    _currentFolder = folder;
    _grid->clear();

    const QDir dir(folder);
    const QStringList files = dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        const QString path = dir.filePath(file);
        const auto pattern = loadPattern(path);
        if (!pattern.has_value()) {
            continue; // skip files that are not valid patterns
        }

        auto* item = new QListWidgetItem(QFileInfo(file).completeBaseName(), _grid);
        item->setData(PATH_ROLE, path);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        const QPixmap thumbnail = pattern::PatternThumbnail::forPattern(
            *pattern, path, _resources, *_hexgrid, THUMBNAIL_SIZE);
        if (!thumbnail.isNull()) {
            item->setIcon(QIcon(thumbnail));
        }
    }
}

void PatternBrowserDialog::onPatternActivated(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const QString path = item->data(PATH_ROLE).toString();
    auto pattern = loadPattern(path);
    if (!pattern.has_value()) {
        return;
    }
    _selected = std::move(pattern);
    accept();
}

void PatternBrowserDialog::onImport() {
    const QString source = QFileDialog::getOpenFileName(
        this, "Import Pattern", QString(), "Gecko Pattern (*.json)");
    if (source.isEmpty()) {
        return;
    }
    // Reject files that don't parse as patterns before copying them in.
    if (!loadPattern(source).has_value()) {
        return;
    }

    const QString target = QDir(_currentFolder).filePath(QFileInfo(source).fileName());
    if (QFileInfo(source) != QFileInfo(target)) {
        QFile::remove(target);
        QFile::copy(source, target);
    }
    populateGrid(_currentFolder);
}

} // namespace geck
