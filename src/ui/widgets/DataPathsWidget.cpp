#include "DataPathsWidget.h"
#include "../../util/Settings.h"

#include <QApplication>
#include <QStyle>
#include <QStandardPaths>
#include <QFileDialog>
#include <QMessageBox>
#include <spdlog/spdlog.h>

namespace geck {

// Constants for styling and configuration
namespace {
    constexpr int LIST_MAX_HEIGHT = 100;
    constexpr const char* HELP_STYLE = "QLabel { color: gray; font-size: 11px; margin-bottom: 8px; }";
    constexpr const char* STATUS_NORMAL = "QLabel { color: gray; font-size: 11px; }";
    constexpr const char* STATUS_WARNING = "QLabel { color: orange; font-size: 11px; }";
    constexpr const char* STATUS_ERROR = "QLabel { color: red; font-size: 11px; }";
    constexpr const char* STATUS_SUCCESS = "QLabel { color: green; font-size: 11px; }";
    constexpr const char* STATUS_INFO = "QLabel { color: blue; font-size: 11px; }";
}

DataPathsWidget::DataPathsWidget(QWidget* parent)
    : QGroupBox("Fallout 2 Data Paths", parent)
    , _layout(nullptr)
    , _helpLabel(nullptr)
    , _pathsList(nullptr)
    , _controlLayout(nullptr)
    , _addButton(nullptr)
    , _removeButton(nullptr)
    , _autoDetectButton(nullptr)
    , _progressBar(nullptr)
    , _statusLabel(nullptr) {
    
    setupUI();
    setupConnections();
}

void DataPathsWidget::setupUI() {
    _layout = new QVBoxLayout(this);
    
    // Help text
    _helpLabel = new QLabel(
        "Add paths to Fallout 2 data directories or .dat files. These will be searched for game resources."
    );
    _helpLabel->setWordWrap(true);
    _helpLabel->setStyleSheet(HELP_STYLE);
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
    
    // Status label
    _statusLabel = new QLabel("Ready");
    _statusLabel->setStyleSheet(STATUS_NORMAL);
    _layout->addWidget(_statusLabel);
}

void DataPathsWidget::setupConnections() {
    connect(_addButton, &QPushButton::clicked, this, &DataPathsWidget::onAddPath);
    connect(_removeButton, &QPushButton::clicked, this, &DataPathsWidget::onRemovePath);
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
        if (_pathsList->item(i)->text() == pathStr) {
            return; // Path already exists
        }
    }
    
    QListWidgetItem* item = new QListWidgetItem(pathStr);
    
    // Set icon based on path type and validity
    auto& settings = Settings::getInstance();
    if (settings.validateDataPath(path)) {
        if (std::filesystem::is_directory(path)) {
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
        }
        item->setToolTip("Valid Fallout 2 data path");
    } else {
        item->setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning));
        item->setToolTip("Invalid or missing path");
        item->setForeground(QColor(Qt::red));
    }
    
    _pathsList->addItem(item);
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
    _statusLabel->setText(message);
    
    if (styleClass == "warning") {
        _statusLabel->setStyleSheet(STATUS_WARNING);
    } else if (styleClass == "error") {
        _statusLabel->setStyleSheet(STATUS_ERROR);
    } else if (styleClass == "success") {
        _statusLabel->setStyleSheet(STATUS_SUCCESS);
    } else if (styleClass == "info") {
        _statusLabel->setStyleSheet(STATUS_INFO);
    } else {
        _statusLabel->setStyleSheet(STATUS_NORMAL);
    }
    
    emit statusChanged(message, styleClass);
}

void DataPathsWidget::updateButtonStates() {
    _removeButton->setEnabled(_pathsList->currentItem() != nullptr);
}

void DataPathsWidget::removeSelectedPath() {
    QListWidgetItem* item = _pathsList->currentItem();
    if (item) {
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

void DataPathsWidget::onItemDoubleClicked(QListWidgetItem* item) {
    if (item) {
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