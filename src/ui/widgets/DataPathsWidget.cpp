#include "DataPathsWidget.h"
#include "ui/Settings.h"
#include "Application.h"
#include "resource/WritableDataRoot.h"
#include "ui/IconHelper.h"
#include "ui/theme/ThemeManager.h"
#include "util/GameDataPathResolver.h"

#include <QApplication>
#include <QStandardPaths>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <algorithm>
#include <array>
#include <optional>
#include <system_error>
#include <spdlog/spdlog.h>

namespace geck {

using namespace ui::constants;

namespace {
    // The path item's tooltip as composed at row creation, before the save-location line is
    // appended — refreshSaveLocationMarkers rebuilds from this so the line never accumulates.
    constexpr int BaseTooltipRole = Qt::UserRole + 1;
}

DataPathsWidget::DataPathsWidget(std::shared_ptr<Settings> settings, QWidget* parent)
    : QGroupBox("Fallout 2 Data Paths", parent)
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
        "Down to reorder.\n"
        "Map saves and name/variable edits are written to the folder marked as the Save Location; if "
        "none is marked, the highest-priority folder is used.");
    _helpLabel->setWordWrap(true);
    _helpLabel->setStyleSheet(ui::theme::styles::helpText());
    _layout->addWidget(_helpLabel);

    _pathsTable = new QTableWidget(0, ColumnCount);
    _pathsTable->setHorizontalHeaderLabels({ "Priority", "Path" });
    _pathsTable->horizontalHeaderItem(PriorityColumn)
        ->setToolTip("1 = highest priority (overrides the sources below it)");
    _pathsTable->horizontalHeaderItem(PriorityColumn)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    _pathsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _pathsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _pathsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _pathsTable->setAlternatingRowColors(true);
    _pathsTable->verticalHeader()->setVisible(false);
    _pathsTable->horizontalHeader()->setSectionResizeMode(PriorityColumn, QHeaderView::ResizeToContents);
    _pathsTable->horizontalHeader()->setSectionResizeMode(PathColumn, QHeaderView::Stretch);
    // Grow with the dialog instead of a fixed 100px cap, so the list shows several rows (the full path
    // for long entries is in each row's tooltip — see addPathRow).
    _pathsTable->setMinimumHeight(LIST_MIN_HEIGHT);
    _pathsTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // The list with its actions in a column beside it, as Qt's own list editors are built: a column
    // cannot wrap, so it needs no width reserved and no height negotiated.
    auto* listRow = new QHBoxLayout();
    listRow->setContentsMargins(0, 0, 0, 0);
    listRow->setSpacing(SPACING_NORMAL);
    listRow->addWidget(_pathsTable, /*stretch=*/1);

    auto* buttonColumn = new QVBoxLayout();
    // Flush with the top of the list: a nested layout otherwise inherits margins from the style.
    buttonColumn->setContentsMargins(0, 0, 0, 0);
    buttonColumn->setSpacing(SPACING_NORMAL);
    _controlLayout = buttonColumn;

    _addButton = new QPushButton("Add Path...");
    _addButton->setIcon(createIcon(":/icons/ui/add.svg"));
    QMenu* addMenu = new QMenu(_addButton);
    addMenu->addAction("Add Folder...", this, &DataPathsWidget::onAddFolder);
    addMenu->addAction("Add DAT File...", this, &DataPathsWidget::onAddDat);
    _addButton->setMenu(addMenu); // clicking pops the menu; folder vs single .dat
    _controlLayout->addWidget(_addButton);

    _removeButton = new QPushButton("Remove");
    _removeButton->setIcon(createIcon(":/icons/ui/remove.svg"));
    _removeButton->setEnabled(false);
    _controlLayout->addWidget(_removeButton);

    _moveUpButton = new QPushButton("Move Up");
    _moveUpButton->setIcon(createIcon(":/icons/actions/move-up.svg"));
    _moveUpButton->setToolTip("Raise priority (closer to the top wins more often)");
    _moveUpButton->setEnabled(false);
    _controlLayout->addWidget(_moveUpButton);

    _moveDownButton = new QPushButton("Move Down");
    _moveDownButton->setIcon(createIcon(":/icons/actions/move-down.svg"));
    _moveDownButton->setToolTip("Lower priority");
    _moveDownButton->setEnabled(false);
    _controlLayout->addWidget(_moveDownButton);

    // Checkable so the label stays constant while the check state shows the marker (it used to grow to
    // "Clear ..." and clip). Below this: what a path *is*, not what to do with it.
    buttonColumn->addSpacing(SPACING_LOOSE);

    _saveLocationButton = new QPushButton("Save Location");
    _saveLocationButton->setIcon(createIcon(":/icons/actions/save.svg"));
    _saveLocationButton->setCheckable(true);
    _saveLocationButton->setToolTip(
        "Write map saves and name/variable edits to the selected folder, regardless of its priority.\n"
        "Only real folders can be a save location — DAT archives are read-only.");
    _saveLocationButton->setEnabled(false);
    _controlLayout->addWidget(_saveLocationButton);

    _scriptSourceButton = new QPushButton("Script Source");
    _scriptSourceButton->setIcon(createIcon(":/icons/filetypes/script.svg"));
    _scriptSourceButton->setCheckable(true);
    _scriptSourceButton->setToolTip(
        "Mark the selected folder as an SSL script-source tree (e.g. the Restoration Project's "
        "scripts_src). \"Edit Script Source\" searches these for a script's .ssl by name and opens "
        "it in your configured editor.\nOnly real folders can be a script source — DAT archives hold "
        "compiled .int, not source.");
    _scriptSourceButton->setEnabled(false);
    _controlLayout->addWidget(_scriptSourceButton);

    _autoDetectButton = new QPushButton("Auto-Detect");
    _autoDetectButton->setIcon(createIcon(":/icons/ui/auto-detect.svg"));
    _autoDetectButton->setToolTip("Automatically detect Fallout 2 installations");
    _controlLayout->addWidget(_autoDetectButton);

    // Consistent icon size + minimum height so the buttons don't clip their icons on resize. The
    // column already gives them a common width, so none needs one forced on it.
    const std::array actionButtons = { _addButton, _removeButton, _moveUpButton, _moveDownButton,
        _saveLocationButton, _scriptSourceButton, _autoDetectButton };
    for (QPushButton* btn : actionButtons) {
        geck::ui::styleActionButton(btn);
    }

    // Pinned to the top of the list; the stretch below takes the slack as the dialog grows.
    buttonColumn->addStretch();
    listRow->addLayout(buttonColumn);
    _layout->addLayout(listRow, /*stretch=*/1);

    _progressBar = new QProgressBar();
    _progressBar->setVisible(false);
    _layout->addWidget(_progressBar);

    _statusLabel = new QLabel();
    _statusLabel->setWordWrap(true);
    _statusLabel->setStyleSheet(ui::theme::styles::statusNormal());
    _statusLabel->setVisible(false);
    _layout->addWidget(_statusLabel);
}

void DataPathsWidget::setupConnections() {
    // _addButton uses a drop-down menu (Add Folder / Add DAT File), wired in setupUI.
    connect(_removeButton, &QPushButton::clicked, this, &DataPathsWidget::onRemovePath);
    connect(_moveUpButton, &QPushButton::clicked, [this]() { moveSelectedPath(-1); });
    connect(_moveDownButton, &QPushButton::clicked, [this]() { moveSelectedPath(1); });
    connect(_saveLocationButton, &QPushButton::clicked, this, &DataPathsWidget::onToggleSaveLocation);
    connect(_scriptSourceButton, &QPushButton::clicked, this, &DataPathsWidget::onToggleScriptSource);
    connect(_autoDetectButton, &QPushButton::clicked, this, &DataPathsWidget::onAutoDetect);
    connect(_pathsTable, &QTableWidget::itemSelectionChanged, this, &DataPathsWidget::onSelectionChanged);
    connect(_pathsTable, &QTableWidget::cellDoubleClicked, this, &DataPathsWidget::onCellDoubleClicked);
}

std::vector<std::filesystem::path> DataPathsWidget::getDataPaths() const {
    // The table shows highest priority first, but the stored order is lowest-first - the order the
    // loader mounts, where the last-mounted source wins. So reverse the rows to recover it.
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

    // A save-location marker whose folder is no longer among the rows can't stay marked.
    if (!_writableDataPath.empty()) {
        const auto current = getDataPaths();
        if (std::none_of(current.begin(), current.end(),
                [this](const std::filesystem::path& entry) { return isMarkedSaveLocation(entry); })) {
            _writableDataPath.clear();
        }
    }
    pruneScriptSourceMarkers(); // likewise, a source marker can't outlive its row

    validatePaths();
    updateButtonStates();
}

std::filesystem::path DataPathsWidget::getWritableDataPath() const {
    return _writableDataPath;
}

void DataPathsWidget::setWritableDataPath(const std::filesystem::path& path) {
    _writableDataPath = path.empty() ? std::filesystem::path{} : Settings::normalizeDataPath(path);
    refreshSaveLocationMarkers();
    updateButtonStates();
}

std::vector<std::filesystem::path> DataPathsWidget::getScriptSourcePaths() const {
    return _scriptSourcePaths;
}

void DataPathsWidget::setScriptSourcePaths(const std::vector<std::filesystem::path>& paths) {
    _scriptSourcePaths.clear();
    for (const auto& path : paths) {
        _scriptSourcePaths.push_back(Settings::normalizeDataPath(path));
    }
    pruneScriptSourceMarkers();
    refreshSaveLocationMarkers();
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
    // Right-aligned so the numbers stack; centred, 1 and 10 start at different x positions.
    priorityItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* pathItem = new QTableWidgetItem(pathStr);

    // The script-source state isn't known yet (setScriptSourcePaths runs after the rows are built), so
    // pass false here; the later refresh corrects the icon.
    auto& settings = *_settings;
    if (settings.validateDataPath(normalizedPath)) {
        if (isDefaultPath) {
            pathItem->setToolTip("Built-in resources path (cannot be removed)");
            QPalette palette = QApplication::palette();
            pathItem->setForeground(palette.color(QPalette::Disabled, QPalette::Text));
        } else {
            pathItem->setToolTip("Valid Fallout 2 data path");
        }
    } else {
        pathItem->setToolTip("Invalid or missing path");
        pathItem->setForeground(ui::theme::colors::invalidPath());
    }
    pathItem->setIcon(iconForRow(normalizedPath, /*isScriptSource=*/false));

    // Prepend the full path so a long entry that doesn't fit the column is still readable on hover
    // (the column stretches and elides the text); the status note above stays as a second line.
    pathItem->setToolTip(pathStr + "\n" + pathItem->toolTip());

    pathItem->setData(Qt::UserRole, isDefaultPath);
    pathItem->setData(BaseTooltipRole, pathItem->toolTip());

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
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            _pathsTable->setItem(row, PriorityColumn, item);
        }
        // Arrow leads so the digits stay flush right, in line with the rows that have none.
        QString text = QString::number(row + 1);
        if (row == 0) {
            text = "▲ " + text; // highest
        } else if (row == rows - 1 && rows > 1) {
            text = "▼ " + text; // lowest
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
    refreshSaveLocationMarkers(); // rows or the marker changed; either moves the badge

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
    // In this section: one line shared with the game location meant the later message won.
    ui::setStatusText(_statusLabel, message, styleClass);
    Q_EMIT statusChanged(message, styleClass);
}

void DataPathsWidget::updateButtonStates() {
    const int row = selectedRow();
    if (row >= 0) {
        const bool protectedRow = isProtectedRow(row);
        // Priority is the user's call, so any row moves; only removal is blocked. The old rule kept
        // rows from passing the built-in folder to keep it lowest, which it never was - its DATs sit below.
        _removeButton->setEnabled(!protectedRow);
        _moveUpButton->setEnabled(row > 0);
        _moveDownButton->setEnabled(row < _pathsTable->rowCount() - 1);
    } else {
        _removeButton->setEnabled(false);
        _moveUpButton->setEnabled(false);
        _moveDownButton->setEnabled(false);
    }

    // Both markers are checkable toggles: enabled for a real folder, checked when the selected row
    // already carries the marker, so clicking clears it.
    const bool markable = isMarkableRow(row);
    _saveLocationButton->setEnabled(markable);
    _saveLocationButton->setChecked(markable && isMarkedSaveLocation(pathAtRow(row)));

    _scriptSourceButton->setEnabled(markable);
    _scriptSourceButton->setChecked(markable
        && std::find(_scriptSourcePaths.begin(), _scriptSourcePaths.end(), pathAtRow(row)) != _scriptSourcePaths.end());
}

std::filesystem::path DataPathsWidget::pathAtRow(int row) const {
    if (row < 0 || row >= _pathsTable->rowCount()) {
        return {};
    }
    QTableWidgetItem* item = _pathsTable->item(row, PathColumn);
    if (!item) {
        return {};
    }
    return Settings::normalizeDataPath(item->text().toStdString());
}

bool DataPathsWidget::isMarkableRow(int row) const {
    if (row < 0 || row >= _pathsTable->rowCount() || isProtectedRow(row)) {
        return false; // the built-in resources folder must not collect user maps
    }
    const std::filesystem::path path = pathAtRow(row);
    std::error_code ec;
    return !path.empty() && std::filesystem::is_directory(path, ec);
}

bool DataPathsWidget::isMarkedSaveLocation(const std::filesystem::path& path) const {
    return !_writableDataPath.empty() && resource::sameDataPathEntry(path, _writableDataPath);
}

void DataPathsWidget::onToggleSaveLocation() {
    const int row = selectedRow();
    if (!isMarkableRow(row)) {
        return;
    }

    const std::filesystem::path path = pathAtRow(row);
    if (isMarkedSaveLocation(path)) {
        _writableDataPath.clear();
        setStatusMessage("Save location cleared — the highest-priority folder will be used.", "info");
    } else {
        _writableDataPath = path;
        setStatusMessage(QString("Save location: %1").arg(QString::fromStdString(path.string())), "success");
    }

    Q_EMIT dataPathsChanged(); // marks the dialog dirty so Apply/OK persists the marker
    refreshSaveLocationMarkers();
    updateButtonStates();
}

void DataPathsWidget::pruneScriptSourceMarkers() {
    const auto paths = getDataPaths();
    std::erase_if(_scriptSourcePaths, [&paths](const std::filesystem::path& marker) {
        return std::find(paths.begin(), paths.end(), marker) == paths.end();
    });
}

QIcon DataPathsWidget::iconForRow(const std::filesystem::path& path, bool isScriptSource) const {
    // Same file-type icon pack the File Browser uses (createIcon → themed tabler SVGs).
    if (!_settings->validateDataPath(path)) {
        return createIcon(":/icons/ui/warning.svg");
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
        return createIcon(":/icons/filetypes/data.svg"); // a .dat archive entry
    }
    if (isScriptSource) {
        return createIcon(":/icons/filetypes/script.svg"); // a folder marked as an SSL source tree
    }
    return createIcon(":/icons/filetypes/folder.svg");
}

void DataPathsWidget::onToggleScriptSource() {
    const int row = selectedRow();
    if (!isMarkableRow(row)) {
        return;
    }

    const std::filesystem::path path = pathAtRow(row);
    if (const auto it = std::find(_scriptSourcePaths.begin(), _scriptSourcePaths.end(), path);
        it != _scriptSourcePaths.end()) {
        _scriptSourcePaths.erase(it);
        setStatusMessage("Script source cleared.", "info");
    } else {
        _scriptSourcePaths.push_back(path);
        setStatusMessage(QString("Script source: %1").arg(QString::fromStdString(path.string())), "success");
    }

    Q_EMIT dataPathsChanged(); // marks the dialog dirty so Apply/OK persists the marker
    refreshSaveLocationMarkers();
    updateButtonStates();
}

void DataPathsWidget::refreshSaveLocationMarkers() {
    const auto paths = getDataPaths();

    // Which ENTRY saves are attributed to right now — the badge marks a row, so this compares in
    // data-path terms (findWritableDataPathEntry), not the `data` subfolder the bytes end up in.
    // Mirrors resource::findWritableDataPath's preference rule — the same equivalence test for
    // membership, via isMarkedSaveLocation — but resolves the fallback locally, so a stale marker
    // does not warn-log on every repaint.
    std::optional<std::filesystem::path> effective;
    if (std::error_code ec; std::any_of(paths.begin(), paths.end(),
                                [this](const std::filesystem::path& entry) { return isMarkedSaveLocation(entry); })
        && std::filesystem::is_directory(_writableDataPath, ec)) {
        effective = _writableDataPath;
    } else {
        effective = resource::findWritableDataPathEntry(paths);
    }

    // The marker only takes effect while it is usable; when it isn't, the badge (and its claim
    // about where saves land) must follow the actual fallback, not the configured wish.
    const bool markerUsable = effective.has_value() && isMarkedSaveLocation(*effective);

    for (int row = 0; row < _pathsTable->rowCount(); ++row) {
        QTableWidgetItem* item = _pathsTable->item(row, PathColumn);
        if (!item) {
            continue;
        }
        const std::filesystem::path rowPath = Settings::normalizeDataPath(item->text().toStdString());
        const bool isExplicit = isMarkedSaveLocation(rowPath);
        const bool isEffective = effective.has_value() && resource::sameDataPathEntry(rowPath, *effective);

        QFont font = _pathsTable->font();
        font.setBold(isExplicit);
        font.setItalic(isEffective && !isExplicit);
        item->setFont(font);

        QString tooltip = item->data(BaseTooltipRole).toString();
        if (isExplicit && markerUsable) {
            tooltip += "\nSave location: map saves and name/variable edits are written here.";
        } else if (isExplicit) {
            tooltip += "\nMarked as the save location, but currently unusable (missing folder) — "
                       "the highest-priority folder is used instead.";
        } else if (isEffective) {
            tooltip += "\nCurrent default save location (highest-priority folder). "
                       "Use \"Save Location\" to pin one explicitly.";
        }
        const bool isSource = std::find(_scriptSourcePaths.begin(), _scriptSourcePaths.end(), rowPath) != _scriptSourcePaths.end();
        if (isSource) {
            tooltip += "\nScript source: searched for a script's .ssl when editing script source.";
        }
        item->setToolTip(tooltip);

        // The icon carries the entry TYPE; the save-location role is shown by font weight, so a folder that
        // is both keeps its script icon.
        item->setIcon(iconForRow(rowPath, isSource));
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

    const bool removedMarked = isMarkedSaveLocation(pathAtRow(row));
    if (removedMarked) {
        _writableDataPath.clear();
    }
    if (const std::filesystem::path removedPath = pathAtRow(row); !removedPath.empty()) {
        std::erase(_scriptSourcePaths, removedPath); // a removed folder can't stay a script source
    }

    _pathsTable->removeRow(row);
    Q_EMIT dataPathsChanged();
    renumberPriorities();
    validatePaths();
    updateButtonStates();

    if (removedMarked) { // after validatePaths so its summary doesn't overwrite this notice
        setStatusMessage("The removed folder was the save location — the highest-priority folder will be used.", "warning");
    }
}

int DataPathsWidget::addFolderExpanded(const std::filesystem::path& folder, bool atTop) {
    // expandDataPaths returns a folder's DATs before the folder itself, so the folder's loose files
    // win. The table shows the stored order reversed (highest priority first), so inserting each row
    // at the top preserves that order and appending has to reverse it to reach the same result.
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
        // Re-picking a marked folder keeps it the save location under its new path.
        const std::filesystem::path oldPath = Settings::normalizeDataPath(currentPath.toStdString());
        const std::filesystem::path newNormalized = Settings::normalizeDataPath(newPath.toStdString());
        if (isMarkedSaveLocation(oldPath)) {
            _writableDataPath = newNormalized;
        }
        // Likewise carry a script-source marker across to the new path.
        if (const auto it = std::find(_scriptSourcePaths.begin(), _scriptSourcePaths.end(), oldPath);
            it != _scriptSourcePaths.end()) {
            *it = newNormalized;
        }
        item->setText(newPath);
        Q_EMIT dataPathsChanged();
        validatePaths();
    }
}

} // namespace geck
