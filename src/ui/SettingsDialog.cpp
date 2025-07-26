#include "SettingsDialog.h"
#include "../util/Settings.h"
#include "../util/QtDialogs.h"

#include <QApplication>
#include <QStyle>
#include <QStandardPaths>
#include <QDir>
#include <spdlog/spdlog.h>

namespace geck {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _dataPathsGroup(nullptr)
    , _dataPathsLayout(nullptr)
    , _pathsControlLayout(nullptr)
    , _dataPathsList(nullptr)
    , _addPathButton(nullptr)
    , _removePathButton(nullptr)
    , _autoDetectButton(nullptr)
    , _pathsHelpLabel(nullptr)
    , _statusLabel(nullptr)
    , _progressBar(nullptr)
    , _buttonBox(nullptr)
    , _applyButton(nullptr)
    , _resetButton(nullptr)
    , _hasChanges(false) {
    
    setWindowTitle("Preferences");
    setModal(true);
    setMinimumSize(600, 400);
    resize(700, 500);
    
    setupUI();
    loadSettings();
    updateUI();
}

void SettingsDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(12, 12, 12, 12);
    _mainLayout->setSpacing(12);
    
    setupDataPathsGroup();
    
    // Status area
    _statusLabel = new QLabel("Ready");
    _statusLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; }");
    _mainLayout->addWidget(_statusLabel);
    
    _progressBar = new QProgressBar();
    _progressBar->setVisible(false);
    _mainLayout->addWidget(_progressBar);
    
    setupButtonBox();
    
    setLayout(_mainLayout);
}

void SettingsDialog::setupDataPathsGroup() {
    _dataPathsGroup = new QGroupBox("Fallout 2 Data Paths");
    _dataPathsLayout = new QVBoxLayout(_dataPathsGroup);
    
    // Help text
    _pathsHelpLabel = new QLabel(
        "Add paths to Fallout 2 data directories or .dat files. These will be searched for game resources."
    );
    _pathsHelpLabel->setWordWrap(true);
    _pathsHelpLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; margin-bottom: 8px; }");
    _dataPathsLayout->addWidget(_pathsHelpLabel);
    
    // Paths list
    _dataPathsList = new QListWidget();
    _dataPathsList->setSelectionMode(QAbstractItemView::SingleSelection);
    _dataPathsList->setAlternatingRowColors(true);
    _dataPathsLayout->addWidget(_dataPathsList);
    
    // Control buttons
    _pathsControlLayout = new QHBoxLayout();
    
    _addPathButton = new QPushButton("Add Path...");
    _addPathButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
    _pathsControlLayout->addWidget(_addPathButton);
    
    _removePathButton = new QPushButton("Remove");
    _removePathButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    _removePathButton->setEnabled(false);
    _pathsControlLayout->addWidget(_removePathButton);
    
    _pathsControlLayout->addStretch();
    
    _autoDetectButton = new QPushButton("Auto-Detect");
    _autoDetectButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    _autoDetectButton->setToolTip("Automatically detect Fallout 2 installations");
    _pathsControlLayout->addWidget(_autoDetectButton);
    
    _dataPathsLayout->addLayout(_pathsControlLayout);
    _mainLayout->addWidget(_dataPathsGroup);
    
    // Connect signals
    connect(_addPathButton, &QPushButton::clicked, this, &SettingsDialog::onAddDataPath);
    connect(_removePathButton, &QPushButton::clicked, this, &SettingsDialog::onRemoveDataPath);
    connect(_autoDetectButton, &QPushButton::clicked, this, &SettingsDialog::onAutoDetect);
    connect(_dataPathsList, &QListWidget::itemSelectionChanged, this, &SettingsDialog::onDataPathSelectionChanged);
    
    // Enable double-click to edit paths
    connect(_dataPathsList, &QListWidget::itemDoubleClicked, [this](QListWidgetItem* item) {
        if (item) {
            QString currentPath = item->text();
            QString newPath = QFileDialog::getExistingDirectory(this, 
                "Select Fallout 2 Data Directory", currentPath);
            if (!newPath.isEmpty() && newPath != currentPath) {
                item->setText(newPath);
                _hasChanges = true;
                updateUI();
            }
        }
    });
}

void SettingsDialog::setupButtonBox() {
    _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    
    _applyButton = _buttonBox->addButton("Apply", QDialogButtonBox::ApplyRole);
    _resetButton = _buttonBox->addButton("Reset", QDialogButtonBox::ResetRole);
    
    _applyButton->setEnabled(false);
    
    connect(_buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_applyButton, &QPushButton::clicked, this, &SettingsDialog::onApply);
    connect(_resetButton, &QPushButton::clicked, this, &SettingsDialog::onReset);
    
    _mainLayout->addWidget(_buttonBox);
}

void SettingsDialog::loadSettings() {
    auto& settings = Settings::getInstance();
    _originalDataPaths = settings.getDataPaths();
    
    // Load data paths into UI
    _dataPathsList->clear();
    for (const auto& path : _originalDataPaths) {
        addDataPathToList(path);
    }
    
    _hasChanges = false;
}

void SettingsDialog::saveSettings() {
    auto& settings = Settings::getInstance();
    
    // Save data paths
    auto dataPaths = getDataPathsFromUI();
    settings.setDataPaths(dataPaths);
    settings.save();
    
    _originalDataPaths = dataPaths;
    _hasChanges = false;
    
    _statusLabel->setText("Settings saved successfully");
    spdlog::info("Settings saved from preferences dialog");
}

void SettingsDialog::updateUI() {
    _applyButton->setEnabled(_hasChanges);
    _removePathButton->setEnabled(_dataPathsList->currentItem() != nullptr);
    
    // Update status based on data paths
    validateDataPaths();
}

void SettingsDialog::validateDataPaths() {
    auto& settings = Settings::getInstance();
    auto dataPaths = getDataPathsFromUI();
    
    if (dataPaths.empty()) {
        _statusLabel->setText("Warning: No data paths configured. Game resources will not be available.");
        _statusLabel->setStyleSheet("QLabel { color: orange; }");
        return;
    }
    
    int validPaths = 0;
    for (const auto& path : dataPaths) {
        if (settings.validateDataPath(path)) {
            validPaths++;
        }
    }
    
    if (validPaths == 0) {
        _statusLabel->setText("Warning: No valid data paths found. Check that paths exist and contain Fallout 2 data.");
        _statusLabel->setStyleSheet("QLabel { color: red; }");
    } else if (validPaths < static_cast<int>(dataPaths.size())) {
        _statusLabel->setText(QString("Found %1 valid paths out of %2 configured.").arg(validPaths).arg(dataPaths.size()));
        _statusLabel->setStyleSheet("QLabel { color: orange; }");
    } else {
        _statusLabel->setText(QString("All %1 data paths are valid.").arg(validPaths));
        _statusLabel->setStyleSheet("QLabel { color: green; }");
    }
}

void SettingsDialog::addDataPathToList(const std::filesystem::path& path) {
    QString pathStr = QString::fromStdString(path.string());
    
    // Check if path already exists in list
    for (int i = 0; i < _dataPathsList->count(); ++i) {
        if (_dataPathsList->item(i)->text() == pathStr) {
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
    
    _dataPathsList->addItem(item);
}

void SettingsDialog::removeSelectedDataPath() {
    QListWidgetItem* item = _dataPathsList->currentItem();
    if (item) {
        delete _dataPathsList->takeItem(_dataPathsList->row(item));
        _hasChanges = true;
        updateUI();
    }
}

std::vector<std::filesystem::path> SettingsDialog::getDataPathsFromUI() const {
    std::vector<std::filesystem::path> paths;
    
    for (int i = 0; i < _dataPathsList->count(); ++i) {
        QListWidgetItem* item = _dataPathsList->item(i);
        if (item) {
            paths.emplace_back(item->text().toStdString());
        }
    }
    
    return paths;
}

// Slots
void SettingsDialog::onAddDataPath() {
    QString path = QFileDialog::getExistingDirectory(this, 
        "Select Fallout 2 Data Directory", 
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    
    if (!path.isEmpty()) {
        addDataPathToList(std::filesystem::path(path.toStdString()));
        _hasChanges = true;
        updateUI();
    }
}

void SettingsDialog::onRemoveDataPath() {
    removeSelectedDataPath();
}

void SettingsDialog::onAutoDetect() {
    _autoDetectButton->setEnabled(false);
    _progressBar->setVisible(true);
    _progressBar->setRange(0, 0); // Indeterminate progress
    _statusLabel->setText("Detecting Fallout 2 installations...");
    
    QApplication::processEvents(); // Update UI
    
    auto detectedPaths = Settings::detectFallout2Installations();
    
    int addedPaths = 0;
    for (const auto& path : detectedPaths) {
        // Check if path is already in the list
        bool exists = false;
        for (int i = 0; i < _dataPathsList->count(); ++i) {
            if (_dataPathsList->item(i)->text() == QString::fromStdString(path.string())) {
                exists = true;
                break;
            }
        }
        
        if (!exists) {
            addDataPathToList(path);
            addedPaths++;
        }
    }
    
    _progressBar->setVisible(false);
    _autoDetectButton->setEnabled(true);
    
    if (addedPaths > 0) {
        _hasChanges = true;
        _statusLabel->setText(QString("Auto-detection complete. Added %1 new path(s).").arg(addedPaths));
        _statusLabel->setStyleSheet("QLabel { color: green; }");
    } else if (detectedPaths.empty()) {
        _statusLabel->setText("No Fallout 2 installations detected automatically.");
        _statusLabel->setStyleSheet("QLabel { color: orange; }");
    } else {
        _statusLabel->setText("Auto-detection complete. All detected paths were already configured.");
        _statusLabel->setStyleSheet("QLabel { color: blue; }");
    }
    
    updateUI();
}

void SettingsDialog::onDataPathSelectionChanged() {
    updateUI();
}

void SettingsDialog::onAccept() {
    if (_hasChanges) {
        saveSettings();
    }
    accept();
}

void SettingsDialog::onApply() {
    saveSettings();
    updateUI();
}

void SettingsDialog::onReset() {
    int ret = QMessageBox::question(this, "Reset Settings", 
        "Are you sure you want to reset all settings to their original values?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        loadSettings();
        updateUI();
    }
}

} // namespace geck