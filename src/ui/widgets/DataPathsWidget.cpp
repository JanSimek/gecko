#include "DataPathsWidget.h"
#include "../../util/Settings.h"
#include "../../Application.h"
#include "../UIConstants.h"

#include <QApplication>
#include <QStyle>
#include <QStandardPaths>
#include <QFileDialog>
#include <QMessageBox>
#include <spdlog/spdlog.h>

namespace geck {

using namespace ui::constants;

DataPathsWidget::DataPathsWidget(QWidget* parent)
    : QGroupBox("Fallout 2 Data Paths", parent)
    , _layout(nullptr)
    , _helpLabel(nullptr)
    , _pathsList(nullptr)
    , _controlLayout(nullptr)
    , _addButton(nullptr)
    , _removeButton(nullptr)
    , _moveUpButton(nullptr)
    , _moveDownButton(nullptr)
    , _autoDetectButton(nullptr)
    , _progressBar(nullptr) {

    setupUI();
    setupConnections();
}

void DataPathsWidget::setupUI() {
    _layout = new QVBoxLayout(this);

    // Help text
    _helpLabel = new QLabel(
        "Add paths to Fallout 2 data directories or .dat files. These will be searched for game resources.\n"
        "Sources are applied in list order: later entries override earlier ones when files have the same path.");
    _helpLabel->setWordWrap(true);
    _helpLabel->setStyleSheet(ui::theme::styles::helpText());
    _layout->addWidget(_helpLabel);

    // Paths list
    _pathsList = new QListWidget();
    _pathsList->setSelectionMode(QAbstractItemView::SingleSelection);
    _pathsList->setAlternatingRowColors(true);
    _pathsList->setMaximumHeight(LIST_MAX_HEIGHT);
    _pathsList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _layout->addWidget(_pathsList);

    // Control buttons
    _controlLayout = new QHBoxLayout();

    _addButton = new QPushButton("Add Path...");
    _addButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
    _controlLayout->addWidget(_addButton);

    _removeButton = new QPushButton("Remove");
    _removeButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    _removeButton->setEnabled(false);
    _controlLayout->addWidget(_removeButton);

    _moveUpButton = new QPushButton("Move Up");
    _moveUpButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowUp));
    _moveUpButton->setEnabled(false);
    _controlLayout->addWidget(_moveUpButton);

    _moveDownButton = new QPushButton("Move Down");
    _moveDownButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowDown));
    _moveDownButton->setEnabled(false);
    _controlLayout->addWidget(_moveDownButton);

    _controlLayout->addStretch();

    _autoDetectButton = new QPushButton("Auto-Detect");
    _autoDetectButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    _autoDetectButton->setToolTip("Automatically detect Fallout 2 installations");
    _controlLayout->addWidget(_autoDetectButton);

    _layout->addLayout(_controlLayout);

    // Progress bar (initially hidden)
    _progressBar = new QProgressBar();
    _progressBar->setVisible(false);
    _layout->addWidget(_progressBar);
}

void DataPathsWidget::setupConnections() {
    connect(_addButton, &QPushButton::clicked, this, &DataPathsWidget::onAddPath);
    connect(_removeButton, &QPushButton::clicked, this, &DataPathsWidget::onRemovePath);
    connect(_moveUpButton, &QPushButton::clicked, [this]() { moveSelectedPath(-1); });
    connect(_moveDownButton, &QPushButton::clicked, [this]() { moveSelectedPath(1); });
    connect(_autoDetectButton, &QPushButton::clicked, this, &DataPathsWidget::onAutoDetect);
    connect(_pathsList, &QListWidget::itemSelectionChanged, this, &DataPathsWidget::onSelectionChanged);
    connect(_pathsList, &QListWidget::itemDoubleClicked, this, &DataPathsWidget::onItemDoubleClicked);
}

std::vector<std::filesystem::path> DataPathsWidget::getDataPaths() const {
    std::vector<std::filesystem::path> paths;

    for (int i = 0; i < _pathsList->count(); ++i) {
        QListWidgetItem* item = _pathsList->item(i);
        if (item) {
            paths.emplace_back(item->text().toStdString());
        }
    }

    return paths;
}

void DataPathsWidget::setDataPaths(const std::vector<std::filesystem::path>& paths) {
    _pathsList->clear();

    for (const auto& path : paths) {
        addPathToList(path);
    }

    validatePaths();
    updateButtonStates();
}

void DataPathsWidget::addPathToList(const std::filesystem::path& path) {
    QString pathStr = QString::fromStdString(path.string());

    // Check if path already exists in list
    for (int i = 0; i < _pathsList->count(); ++i) {
        QListWidgetItem* existingItem = _pathsList->item(i);
        if (existingItem && existingItem->text() == pathStr) {
            return; // Path already exists
        }
    }

    bool isDefaultPath = Application::isDefaultResourcesPath(path);

    QListWidgetItem* item = new QListWidgetItem(pathStr);

    // Set icon based on path type and validity
    auto& settings = Settings::getInstance();
    if (settings.validateDataPath(path)) {
        if (isDefaultPath) {
            item->setToolTip("Built-in resources path (cannot be removed)");
            // Use disabled color from the palette
            QPalette palette = QApplication::palette();
            item->setForeground(palette.color(QPalette::Disabled, QPalette::Text));
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        } else if (std::filesystem::is_directory(path)) {
            item->setToolTip("Valid Fallout 2 data path");
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
            item->setToolTip("Valid Fallout 2 data path");
        }
    } else {
        item->setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning));
        item->setToolTip("Invalid or missing path");
        item->setForeground(ui::theme::colors::invalidPath());
    }

    // Store whether this is a protected path in the item data
    item->setData(Qt::UserRole, isDefaultPath);

    _pathsList->addItem(item);
    _pathsList->setCurrentItem(item);
}

void DataPathsWidget::validatePaths() {
    auto& settings = Settings::getInstance();
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
    emit statusChanged(message, styleClass);
}

void DataPathsWidget::updateButtonStates() {
    QListWidgetItem* currentItem = _pathsList->currentItem();
    if (currentItem) {
        bool isProtected = currentItem->data(Qt::UserRole).toBool();
        _removeButton->setEnabled(!isProtected);

        int currentRow = _pathsList->row(currentItem);
        _moveUpButton->setEnabled(currentRow > 0);
        _moveDownButton->setEnabled(currentRow < _pathsList->count() - 1);
    } else {
        _removeButton->setEnabled(false);
        _moveUpButton->setEnabled(false);
        _moveDownButton->setEnabled(false);
    }
}

void DataPathsWidget::removeSelectedPath() {
    QListWidgetItem* item = _pathsList->currentItem();
    if (item) {
        bool isProtected = item->data(Qt::UserRole).toBool();
        if (isProtected) {
            QMessageBox::warning(this, "Cannot Remove Path",
                "The built-in resources path cannot be removed as it contains essential game assets.");
            return;
        }

        delete _pathsList->takeItem(_pathsList->row(item));
        emit dataPathsChanged();
        validatePaths();
        updateButtonStates();
    }
}

void DataPathsWidget::onAddPath() {
    QString path = QFileDialog::getExistingDirectory(this,
        "Select Fallout 2 Data Directory",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));

    if (!path.isEmpty()) {
        addPathToList(std::filesystem::path(path.toStdString()));
        emit dataPathsChanged();
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

    QApplication::processEvents(); // Update UI

    auto detectedPaths = Settings::detectFallout2Installations();

    _progressBar->setVisible(false);
    _autoDetectButton->setEnabled(true);

    int addedPaths = 0;
    for (const auto& path : detectedPaths) {
        // Check if path is already in the list
        bool exists = false;
        for (int i = 0; i < _pathsList->count(); ++i) {
            if (_pathsList->item(i)->text() == QString::fromStdString(path.string())) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            addPathToList(path);
            addedPaths++;
        }
    }

    if (addedPaths > 0) {
        emit dataPathsChanged();
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
    QListWidgetItem* currentItem = _pathsList->currentItem();
    if (!currentItem) {
        return;
    }

    int currentRow = _pathsList->row(currentItem);
    int targetRow = currentRow + offset;
    if (targetRow < 0 || targetRow >= _pathsList->count()) {
        return;
    }

    QListWidgetItem* takenItem = _pathsList->takeItem(currentRow);
    _pathsList->insertItem(targetRow, takenItem);
    _pathsList->setCurrentItem(takenItem);

    emit dataPathsChanged();
    validatePaths();
    updateButtonStates();
}

void DataPathsWidget::onItemDoubleClicked(QListWidgetItem* item) {
    if (item) {
        // Check if this is a protected path
        bool isProtected = item->data(Qt::UserRole).toBool();
        if (isProtected) {
            QMessageBox::information(this, "Cannot Edit Path",
                "The built-in resources path cannot be modified as it contains essential game assets.");
            return;
        }

        QString currentPath = item->text();
        QString newPath = QFileDialog::getExistingDirectory(this,
            "Select Fallout 2 Data Directory", currentPath);
        if (!newPath.isEmpty() && newPath != currentPath) {
            item->setText(newPath);
            emit dataPathsChanged();
            validatePaths();
        }
    }
}

} // namespace geck
