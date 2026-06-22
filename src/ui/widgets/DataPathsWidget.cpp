#include "DataPathsWidget.h"
#include "ui/Settings.h"
#include "Application.h"
#include "ui/UIConstants.h"
#include "util/GameDataPathResolver.h"

#include <QApplication>
#include <QStyle>
#include <QStandardPaths>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace geck {

using namespace ui::constants;

DataPathsWidget::DataPathsWidget(std::shared_ptr<Settings> settings, QWidget* parent)
    : QGroupBox("Fallout 2 Data Paths", parent)
    , _layout(nullptr)
    , _helpLabel(nullptr)
    , _pathsTable(nullptr)
    , _controlLayout(nullptr)
    , _addButton(nullptr)
    , _removeButton(nullptr)
    , _moveUpButton(nullptr)
    , _moveDownButton(nullptr)
    , _autoDetectButton(nullptr)
    , _progressBar(nullptr)
    , _settings(std::move(settings)) {

    setupUI();
    setupConnections();
}

void DataPathsWidget::setupUI() {
    _layout = new QVBoxLayout(this);

    _helpLabel = new QLabel(
        "Add a Fallout 2 folder (its master.dat and critter.dat are added as separate entries you can "
        "reorder or remove) or a single .dat file. Every source is searched together; when the same "
        "file is present in more than one, the higher-priority source wins.\n"
        "The top entry has the highest priority and overrides the ones below it — use Move Up / Move "
        "Down to reorder.");
    _helpLabel->setWordWrap(true);
    _helpLabel->setStyleSheet(ui::theme::styles::helpText());
    _layout->addWidget(_helpLabel);

    _pathsTable = new QTableWidget(0, ColumnCount);
    _pathsTable->setHorizontalHeaderLabels({ "Priority", "Path" });
    _pathsTable->horizontalHeaderItem(PriorityColumn)
        ->setToolTip("1 = highest priority (overrides the sources below it)");
    _pathsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _pathsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _pathsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _pathsTable->setAlternatingRowColors(true);
    _pathsTable->verticalHeader()->setVisible(false);
    _pathsTable->horizontalHeader()->setSectionResizeMode(PriorityColumn, QHeaderView::ResizeToContents);
    _pathsTable->horizontalHeader()->setSectionResizeMode(PathColumn, QHeaderView::Stretch);
    _pathsTable->setMaximumHeight(LIST_MAX_HEIGHT);
    _pathsTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _layout->addWidget(_pathsTable);

    _controlLayout = new QHBoxLayout();

    _addButton = new QPushButton("Add Path...");
    _addButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
    QMenu* addMenu = new QMenu(_addButton);
    addMenu->addAction("Add Folder...", this, &DataPathsWidget::onAddFolder);
    addMenu->addAction("Add DAT File...", this, &DataPathsWidget::onAddDat);
    _addButton->setMenu(addMenu); // clicking pops the menu; folder vs single .dat
    _controlLayout->addWidget(_addButton);

    _removeButton = new QPushButton("Remove");
    _removeButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    _removeButton->setEnabled(false);
    _controlLayout->addWidget(_removeButton);

    _moveUpButton = new QPushButton("Move Up");
    _moveUpButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowUp));
    _moveUpButton->setToolTip("Raise priority (closer to the top wins more often)");
    _moveUpButton->setEnabled(false);
    _controlLayout->addWidget(_moveUpButton);

    _moveDownButton = new QPushButton("Move Down");
    _moveDownButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowDown));
    _moveDownButton->setToolTip("Lower priority");
    _moveDownButton->setEnabled(false);
    _controlLayout->addWidget(_moveDownButton);

    _controlLayout->addStretch();

    _autoDetectButton = new QPushButton("Auto-Detect");
    _autoDetectButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    _autoDetectButton->setToolTip("Automatically detect Fallout 2 installations");
    _controlLayout->addWidget(_autoDetectButton);

    _layout->addLayout(_controlLayout);

    _progressBar = new QProgressBar();
    _progressBar->setVisible(false);
    _layout->addWidget(_progressBar);
}

void DataPathsWidget::setupConnections() {
    // _addButton uses a drop-down menu (Add Folder / Add DAT File), wired in setupUI.
    connect(_removeButton, &QPushButton::clicked, this, &DataPathsWidget::onRemovePath);
    connect(_moveUpButton, &QPushButton::clicked, [this]() { moveSelectedPath(-1); });
    connect(_moveDownButton, &QPushButton::clicked, [this]() { moveSelectedPath(1); });
    connect(_autoDetectButton, &QPushButton::clicked, this, &DataPathsWidget::onAutoDetect);
    connect(_pathsTable, &QTableWidget::itemSelectionChanged, this, &DataPathsWidget::onSelectionChanged);
    connect(_pathsTable, &QTableWidget::cellDoubleClicked, this, &DataPathsWidget::onCellDoubleClicked);
}

std::vector<std::filesystem::path> DataPathsWidget::getDataPaths() const {
    // The table shows the highest priority at the top, but the stored order is lowest-priority-first
    // (that is the order the loader mounts, where the last-mounted source wins). So read the rows
    // top-to-bottom and reverse them to recover the stored order.
    std::vector<std::filesystem::path> paths;

    for (int row = _pathsTable->rowCount() - 1; row >= 0; --row) {
        QTableWidgetItem* item = _pathsTable->item(row, PathColumn);
        if (!item) {
            continue;
        }
        const std::filesystem::path normalizedPath = Settings::normalizeDataPath(item->text().toStdString());
        if (std::find(paths.begin(), paths.end(), normalizedPath) == paths.end()) {
            paths.emplace_back(normalizedPath);
        }
    }

    return paths;
}

void DataPathsWidget::setDataPaths(const std::vector<std::filesystem::path>& paths) {
    _pathsTable->setRowCount(0);

    // paths are lowest-priority-first; append them in reverse so the highest priority lands on top.
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
        addPathRow(*it, /*atTop=*/false);
    }

    renumberPriorities();
    validatePaths();
    updateButtonStates();
}

bool DataPathsWidget::addPathRow(const std::filesystem::path& path, bool atTop) {
    const std::filesystem::path normalizedPath = Settings::normalizeDataPath(path);
    QString pathStr = QString::fromStdString(normalizedPath.string());

    for (int row = 0; row < _pathsTable->rowCount(); ++row) {
        QTableWidgetItem* existing = _pathsTable->item(row, PathColumn);
        if (existing && existing->text() == pathStr) {
            return false;
        }
    }

    bool isDefaultPath = Application::isDefaultResourcesPath(normalizedPath);

    auto* priorityItem = new QTableWidgetItem();
    priorityItem->setTextAlignment(Qt::AlignCenter);

    auto* pathItem = new QTableWidgetItem(pathStr);

    // Icon, tooltip and colour reflect the path type and validity (matches the previous list view).
    auto& settings = *_settings;
    if (settings.validateDataPath(normalizedPath)) {
        if (isDefaultPath) {
            pathItem->setToolTip("Built-in resources path (cannot be removed)");
            QPalette palette = QApplication::palette();
            pathItem->setForeground(palette.color(QPalette::Disabled, QPalette::Text));
            pathItem->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        } else if (std::filesystem::is_directory(normalizedPath)) {
            pathItem->setToolTip("Valid Fallout 2 data path");
            pathItem->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            pathItem->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
            pathItem->setToolTip("Valid Fallout 2 data path");
        }
    } else {
        pathItem->setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning));
        pathItem->setToolTip("Invalid or missing path");
        pathItem->setForeground(ui::theme::colors::invalidPath());
    }

    pathItem->setData(Qt::UserRole, isDefaultPath);

    const int row = atTop ? 0 : _pathsTable->rowCount();
    _pathsTable->insertRow(row);
    _pathsTable->setItem(row, PriorityColumn, priorityItem);
    _pathsTable->setItem(row, PathColumn, pathItem);
    _pathsTable->setCurrentCell(row, PathColumn);

    return true;
}

void DataPathsWidget::renumberPriorities() {
    // Top row is priority 1 (highest); a trailing marker spells out the extremes for the user.
    const int rows = _pathsTable->rowCount();
    for (int row = 0; row < rows; ++row) {
        QTableWidgetItem* item = _pathsTable->item(row, PriorityColumn);
        if (!item) {
            item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignCenter);
            _pathsTable->setItem(row, PriorityColumn, item);
        }
        QString text = QString::number(row + 1);
        if (row == 0) {
            text += " ▲"; // highest
        } else if (row == rows - 1 && rows > 1) {
            text += " ▼"; // lowest
        }
        item->setText(text);
    }
}

bool DataPathsWidget::isProtectedRow(int row) const {
    if (row < 0 || row >= _pathsTable->rowCount()) {
        return false;
    }
    QTableWidgetItem* item = _pathsTable->item(row, PathColumn);
    return item && item->data(Qt::UserRole).toBool();
}

int DataPathsWidget::selectedRow() const {
    return _pathsTable->currentRow();
}

void DataPathsWidget::validatePaths() {
    auto& settings = *_settings;
    auto dataPaths = getDataPaths();

    if (dataPaths.empty()) {
        setStatusMessage("Warning: No data paths configured. Game resources will not be available.", "warning");
        return;
    }

    int validPaths = 0;
    for (const auto& path : dataPaths) {
        if (settings.validateDataPath(path)) {
            validPaths++;
        }
    }

    if (validPaths == 0) {
        setStatusMessage("Warning: No valid data paths found. Check that paths exist and contain Fallout 2 data.", "error");
    } else if (validPaths < static_cast<int>(dataPaths.size())) {
        setStatusMessage(QString("Found %1 valid paths out of %2 configured.").arg(validPaths).arg(dataPaths.size()), "warning");
    } else {
        setStatusMessage(QString("All %1 data paths are valid.").arg(validPaths), "success");
    }
}

void DataPathsWidget::setStatusMessage(const QString& message, const QString& styleClass) {
    Q_EMIT statusChanged(message, styleClass);
}

void DataPathsWidget::updateButtonStates() {
    const int row = selectedRow();
    if (row >= 0) {
        const bool protectedRow = isProtectedRow(row);
        _removeButton->setEnabled(!protectedRow);
        // The built-in resources path is pinned to the bottom (lowest priority): it cannot move,
        // and no other row may be pushed below it.
        _moveUpButton->setEnabled(row > 0 && !protectedRow && !isProtectedRow(row - 1));
        _moveDownButton->setEnabled(row < _pathsTable->rowCount() - 1 && !protectedRow && !isProtectedRow(row + 1));
    } else {
        _removeButton->setEnabled(false);
        _moveUpButton->setEnabled(false);
        _moveDownButton->setEnabled(false);
    }
}

void DataPathsWidget::removeSelectedPath() {
    const int row = selectedRow();
    if (row < 0) {
        return;
    }

    if (isProtectedRow(row)) {
        QMessageBox::warning(this, "Cannot Remove Path",
            "The built-in resources path cannot be removed as it contains essential game assets.");
        return;
    }

    _pathsTable->removeRow(row);
    Q_EMIT dataPathsChanged();
    renumberPriorities();
    validatePaths();
    updateButtonStates();
}

int DataPathsWidget::addFolderExpanded(const std::filesystem::path& folder, bool atTop) {
    // expandDataPaths returns the folder before its DATs (legacy mount order). Inserting each atTop
    // reverses that into the table (DATs above the folder); to get the same table layout when appending
    // at the bottom, append in reverse. Either way the DATs keep their legacy priority over the folder.
    auto expanded = util::expandDataPaths({ folder });
    if (!atTop) {
        std::reverse(expanded.begin(), expanded.end());
    }
    int added = 0;
    for (const auto& entry : expanded) {
        if (addPathRow(entry, atTop)) {
            ++added;
        }
    }
    return added;
}

void DataPathsWidget::onAddFolder() {
    const QString path = QFileDialog::getExistingDirectory(this,
        "Select Fallout 2 Data Folder",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    if (path.isEmpty()) {
        return;
    }

    // A newly added source takes the highest priority (top), matching what a user adding a mod expects.
    if (addFolderExpanded(std::filesystem::path(path.toStdString()), /*atTop=*/true) > 0) {
        Q_EMIT dataPathsChanged();
        renumberPriorities();
        validatePaths();
        updateButtonStates();
    }
}

void DataPathsWidget::onAddDat() {
    const QString path = QFileDialog::getOpenFileName(this,
        "Select Fallout 2 Data File",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        "Fallout 2 data (*.dat);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    if (addPathRow(std::filesystem::path(path.toStdString()), /*atTop=*/true)) {
        Q_EMIT dataPathsChanged();
        renumberPriorities();
        validatePaths();
        updateButtonStates();
    }
}

void DataPathsWidget::onRemovePath() {
    removeSelectedPath();
}

void DataPathsWidget::onAutoDetect() {
    _autoDetectButton->setEnabled(false);
    _progressBar->setVisible(true);
    _progressBar->setRange(0, 0); // Indeterminate progress
    setStatusMessage("Detecting Fallout 2 installations...", "normal");

    QApplication::processEvents();

    auto detectedPaths = Settings::detectFallout2Installations();

    _progressBar->setVisible(false);
    _autoDetectButton->setEnabled(true);

    int addedPaths = 0;
    for (const auto& path : detectedPaths) {
        // Detected base-game installs are appended as lower priority; manually added mods stay on top.
        // Expand each into the folder + its master.dat/critter.dat so the DATs are mounted and listed.
        addedPaths += addFolderExpanded(path, /*atTop=*/false);
    }

    if (addedPaths > 0) {
        renumberPriorities();
        Q_EMIT dataPathsChanged();
        setStatusMessage(QString("Auto-detection complete. Added %1 new path(s).").arg(addedPaths), "success");
    } else if (detectedPaths.empty()) {
        setStatusMessage("No Fallout 2 installations detected automatically.", "warning");
    } else {
        setStatusMessage("Auto-detection complete. All detected paths were already configured.", "info");
    }

    validatePaths();
    updateButtonStates();
}

void DataPathsWidget::onSelectionChanged() {
    updateButtonStates();
}

void DataPathsWidget::moveSelectedPath(int offset) {
    const int row = selectedRow();
    if (row < 0) {
        return;
    }

    const int targetRow = row + offset;
    if (targetRow < 0 || targetRow >= _pathsTable->rowCount()) {
        return;
    }

    // Keep the built-in resources path pinned to the bottom: never move it, and never swap a
    // row into its slot (which would make the built-in outrank a user source).
    if (isProtectedRow(row) || isProtectedRow(targetRow)) {
        return;
    }

    // Swap the path cells; the priority cells stay put and are renumbered below.
    QTableWidgetItem* moving = _pathsTable->takeItem(row, PathColumn);
    QTableWidgetItem* other = _pathsTable->takeItem(targetRow, PathColumn);
    _pathsTable->setItem(row, PathColumn, other);
    _pathsTable->setItem(targetRow, PathColumn, moving);
    _pathsTable->setCurrentCell(targetRow, PathColumn);

    Q_EMIT dataPathsChanged();
    renumberPriorities();
    validatePaths();
    updateButtonStates();
}

void DataPathsWidget::onCellDoubleClicked(int row, int /*column*/) {
    if (row < 0) {
        return;
    }

    if (isProtectedRow(row)) {
        QMessageBox::information(this, "Cannot Edit Path",
            "The built-in resources path cannot be modified as it contains essential game assets.");
        return;
    }

    QTableWidgetItem* item = _pathsTable->item(row, PathColumn);
    if (!item) {
        return;
    }

    // Re-pick with a dialog that matches the entry's kind: a .dat entry needs a file chooser,
    // a folder entry a directory chooser.
    const QString currentPath = item->text();
    QString newPath;
    if (currentPath.endsWith(".dat", Qt::CaseInsensitive)) {
        newPath = QFileDialog::getOpenFileName(this, "Select Fallout 2 Data File",
            currentPath, "Fallout 2 data (*.dat);;All files (*)");
    } else {
        newPath = QFileDialog::getExistingDirectory(this, "Select Fallout 2 Data Directory", currentPath);
    }
    if (!newPath.isEmpty() && newPath != currentPath) {
        item->setText(newPath);
        Q_EMIT dataPathsChanged();
        validatePaths();
    }
}

} // namespace geck
