#include "FrmSelectorDialog.h"

#include <QApplication>
#include <QPixmap>
#include <spdlog/spdlog.h>
#include <regex>
#include <algorithm>
#include <functional>

#include "../../util/ResourceManager.h"
#include "../../util/CritterFrmResolver.h"
#include "../../format/frm/Frm.h"
#include "../../format/lst/Lst.h"

namespace geck {

FrmSelectorDialog::FrmSelectorDialog(QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _splitter(nullptr)
    , _listPanel(nullptr)
    , _listLayout(nullptr)
    , _searchEdit(nullptr)
    , _frmTreeWidget(nullptr)
    , _previewPanel(nullptr)
    , _previewLayout(nullptr)
    , _previewGroup(nullptr)
    , _previewLabel(nullptr)
    , _detailsGroup(nullptr)
    , _detailsLayout(nullptr)
    , _frmPidSpin(nullptr)
    , _frmPathEdit(nullptr)
    , _buttonLayout(nullptr)
    , _okButton(nullptr)
    , _cancelButton(nullptr)
    , _selectedFrmPid(0)
    , _objectTypeFilter(0xFFFFFFFF) { // No filter by default
    
    setupUI();
    populateFrmList();
}

void FrmSelectorDialog::setupUI() {
    setWindowTitle("Select FRM File");
    setModal(true);
    resize(800, 600);
    
    _mainLayout = new QVBoxLayout(this);
    
    // Create splitter for main content
    _splitter = new QSplitter(Qt::Horizontal, this);
    
    // Left panel - FRM list
    _listPanel = new QWidget();
    _listLayout = new QVBoxLayout(_listPanel);
    
    // Search box
    _searchEdit = new QLineEdit();
    _searchEdit->setPlaceholderText("Search FRM files...");
    connect(_searchEdit, &QLineEdit::textChanged, this, &FrmSelectorDialog::onSearchTextChanged);
    _listLayout->addWidget(_searchEdit);
    
    // FRM tree
    _frmTreeWidget = new QTreeWidget();
    _frmTreeWidget->setHeaderLabel("FRM Files");
    _frmTreeWidget->setAlternatingRowColors(true);
    connect(_frmTreeWidget, &QTreeWidget::itemSelectionChanged, this, &FrmSelectorDialog::onFrmListSelectionChanged);
    _listLayout->addWidget(_frmTreeWidget);
    
    // Right panel - Preview and details
    _previewPanel = new QWidget();
    _previewLayout = new QVBoxLayout(_previewPanel);
    
    // Preview group
    _previewGroup = new QGroupBox("Preview");
    QVBoxLayout* previewGroupLayout = new QVBoxLayout(_previewGroup);
    
    _previewLabel = new QLabel("No FRM selected");
    _previewLabel->setAlignment(Qt::AlignCenter);
    _previewLabel->setMinimumHeight(200);
    _previewLabel->setMinimumWidth(200);
    _previewLabel->setScaledContents(false);
    _previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _previewLabel->setStyleSheet("border: 1px solid gray; background-color: #f0f0f0;");
    previewGroupLayout->addWidget(_previewLabel);
    
    // Details group
    _detailsGroup = new QGroupBox("Details");
    _detailsLayout = new QFormLayout(_detailsGroup);
    
    _frmPidSpin = new QSpinBox();
    _frmPidSpin->setRange(0, INT_MAX);
    _frmPidSpin->setReadOnly(true);
    _frmPidSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(_frmPidSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &FrmSelectorDialog::onFrmPidChanged);
    _detailsLayout->addRow("FRM PID:", _frmPidSpin);
    
    _frmPathEdit = new QLineEdit();
    _frmPathEdit->setReadOnly(true);
    _detailsLayout->addRow("FRM Path:", _frmPathEdit);
    
    _previewLayout->addWidget(_previewGroup);
    _previewLayout->addWidget(_detailsGroup);
    
    // Set initial splitter sizes (60% list, 40% preview)
    _splitter->addWidget(_listPanel);
    _splitter->addWidget(_previewPanel);
    _splitter->setSizes({480, 320});
    
    _mainLayout->addWidget(_splitter);
    
    // Button box
    _buttonLayout = new QHBoxLayout();
    _buttonLayout->addStretch();
    
    _cancelButton = new QPushButton("Cancel");
    connect(_cancelButton, &QPushButton::clicked, this, &FrmSelectorDialog::onRejected);
    _buttonLayout->addWidget(_cancelButton);
    
    _okButton = new QPushButton("OK");
    _okButton->setDefault(true);
    _okButton->setEnabled(false);
    connect(_okButton, &QPushButton::clicked, this, &FrmSelectorDialog::onAccepted);
    _buttonLayout->addWidget(_okButton);
    
    _mainLayout->addLayout(_buttonLayout);
}

void FrmSelectorDialog::populateFrmList() {
    try {
        auto& resourceManager = ResourceManager::getInstance();
        auto frmFiles = resourceManager.listFilesByPattern("*.frm");
        
        _frmFiles.clear();
        _frmTreeWidget->clear();
        
        // Group files by type and base name for better organization
        std::map<std::string, std::vector<std::string>> groupedFiles;
        
        for (const auto& frmPath : frmFiles) {
            // Apply object type filter if set
            if (_objectTypeFilter != 0xFFFFFFFF) {
                uint32_t frmType = 0; // Default to items
                
                // Determine FRM type from path
                if (frmPath.find("art/critters/") != std::string::npos) {
                    frmType = 1; // Critters
                } else if (frmPath.find("art/items/") != std::string::npos) {
                    frmType = 0; // Items
                } else if (frmPath.find("art/scenery/") != std::string::npos) {
                    frmType = 2; // Scenery
                } else if (frmPath.find("art/walls/") != std::string::npos) {
                    frmType = 3; // Walls
                } else if (frmPath.find("art/tiles/") != std::string::npos) {
                    frmType = 4; // Tiles
                } else if (frmPath.find("art/misc/") != std::string::npos) {
                    frmType = 5; // Misc
                }
                
                // Skip if this FRM type doesn't match the filter
                if (frmType != _objectTypeFilter) {
                    continue;
                }
            }
            
            // Store path as the key, we'll determine PID when needed
            _frmFiles.emplace_back(0, frmPath); // PID will be resolved dynamically
            
            // Determine grouping key
            std::string groupKey = getGroupingKey(frmPath);
            groupedFiles[groupKey].push_back(frmPath);
        }
        
        // Create root items for different categories
        QTreeWidgetItem* itemsRoot = nullptr;
        QTreeWidgetItem* crittersRoot = nullptr;
        QTreeWidgetItem* sceneryRoot = nullptr;
        QTreeWidgetItem* wallsRoot = nullptr;
        QTreeWidgetItem* tilesRoot = nullptr;
        QTreeWidgetItem* miscRoot = nullptr;
        
        // Add grouped items to the tree
        for (const auto& group : groupedFiles) {
            const std::string& groupName = group.first;
            const auto& files = group.second;
            
            // Determine which root category this group belongs to
            QTreeWidgetItem* parentItem = nullptr;
            if (!files.empty()) {
                const std::string& samplePath = files.front();
                if (samplePath.find("art/critters/") != std::string::npos) {
                    if (!crittersRoot) {
                        crittersRoot = new QTreeWidgetItem(_frmTreeWidget);
                        crittersRoot->setText(0, "Critters");
                        crittersRoot->setExpanded(true);
                    }
                    parentItem = crittersRoot;
                } else if (samplePath.find("art/items/") != std::string::npos) {
                    if (!itemsRoot) {
                        itemsRoot = new QTreeWidgetItem(_frmTreeWidget);
                        itemsRoot->setText(0, "Items");
                        itemsRoot->setExpanded(true);
                    }
                    parentItem = itemsRoot;
                } else if (samplePath.find("art/scenery/") != std::string::npos) {
                    if (!sceneryRoot) {
                        sceneryRoot = new QTreeWidgetItem(_frmTreeWidget);
                        sceneryRoot->setText(0, "Scenery");
                        sceneryRoot->setExpanded(true);
                    }
                    parentItem = sceneryRoot;
                } else if (samplePath.find("art/walls/") != std::string::npos) {
                    if (!wallsRoot) {
                        wallsRoot = new QTreeWidgetItem(_frmTreeWidget);
                        wallsRoot->setText(0, "Walls");
                        wallsRoot->setExpanded(true);
                    }
                    parentItem = wallsRoot;
                } else if (samplePath.find("art/tiles/") != std::string::npos) {
                    if (!tilesRoot) {
                        tilesRoot = new QTreeWidgetItem(_frmTreeWidget);
                        tilesRoot->setText(0, "Tiles");
                        tilesRoot->setExpanded(true);
                    }
                    parentItem = tilesRoot;
                } else if (samplePath.find("art/misc/") != std::string::npos) {
                    if (!miscRoot) {
                        miscRoot = new QTreeWidgetItem(_frmTreeWidget);
                        miscRoot->setText(0, "Misc");
                        miscRoot->setExpanded(true);
                    }
                    parentItem = miscRoot;
                }
            }
            
            // Create group node if multiple files and it's a critter group
            QTreeWidgetItem* groupNode = nullptr;
            if (files.size() > 1 && isCritterGroup(groupName) && parentItem) {
                groupNode = new QTreeWidgetItem(parentItem);
                groupNode->setText(0, QString::fromStdString(groupName));
                groupNode->setData(0, Qt::UserRole, QString("__GROUP_HEADER__"));
            }
            
            // Sort files within group for better presentation
            std::vector<std::string> sortedFiles = files;
            std::sort(sortedFiles.begin(), sortedFiles.end(), [this](const std::string& a, const std::string& b) {
                return getAnimationSortKey(a) < getAnimationSortKey(b);
            });
            
            // Add individual files
            for (const auto& frmPath : sortedFiles) {
                QString displayName = createDisplayName(frmPath);
                
                QTreeWidgetItem* targetParent = groupNode ? groupNode : parentItem;
                if (!targetParent) {
                    // If no parent determined, add to root
                    targetParent = _frmTreeWidget->invisibleRootItem();
                }
                
                auto* item = new QTreeWidgetItem(targetParent);
                item->setText(0, displayName);
                item->setData(0, Qt::UserRole, QString::fromStdString(frmPath));
            }
        }
        
        spdlog::debug("FrmSelectorDialog: Loaded {} FRM files in {} groups", _frmFiles.size(), groupedFiles.size());
        
    } catch (const std::exception& e) {
        spdlog::error("FrmSelectorDialog: Failed to populate FRM list: {}", e.what());
    }
}

void FrmSelectorDialog::updatePreview() {
    // Get the current FRM path directly from the UI
    QString frmPath = _frmPathEdit->text();
    
    if (frmPath.isEmpty()) {
        _previewLabel->clear();
        _previewLabel->setText("No FRM selected");
        return;
    }
    
    try {
        auto& resourceManager = ResourceManager::getInstance();
        std::string frmPathStr = frmPath.toStdString();
        
        // Load texture from ResourceManager using the direct path
        const auto& texture = resourceManager.texture(frmPathStr);
        
        // Convert SFML texture to QImage for processing
        auto image = texture.copyToImage();
        const std::uint8_t* pixels = image.getPixelsPtr();
        QImage qImage(pixels, image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);
        
        // Try to load the FRM file to get frame information
        try {
            auto frm = resourceManager.loadResource<Frm>(frmPathStr);
            if (frm && !frm->directions().empty() && !frm->directions()[0].frames().empty()) {
                // This is a multi-frame FRM (like critters), extract the first frame
                const auto& firstFrame = frm->directions()[0].frames()[0];
                
                // Calculate texture rectangle for the first frame (direction 0)
                uint16_t left = 0;
                uint16_t top = 0; // First direction
                uint16_t width = firstFrame.width();
                uint16_t height = firstFrame.height();
                
                // Extract only the first frame from the full spritesheet
                if (left + width <= qImage.width() && top + height <= qImage.height()) {
                    qImage = qImage.copy(left, top, width, height);
                }
                
                spdlog::debug("FrmSelectorDialog: Extracted first frame {}x{} from FRM: {}", 
                             width, height, frmPathStr);
            }
        } catch (const std::exception& e) {
            // If FRM loading fails, just use the full texture (fallback for simple FRMs)
            spdlog::debug("FrmSelectorDialog: Using full texture for FRM preview ({}): {}", 
                         frmPathStr, e.what());
        }
        
        // Create pixmap and scale to fit label while maintaining aspect ratio
        QPixmap pixmap = QPixmap::fromImage(qImage);
        if (!pixmap.isNull()) {
            // Scale the pixmap to fit within the preview area while keeping aspect ratio
            QSize maxSize = _previewLabel->size();
            if (maxSize.width() < 100 || maxSize.height() < 100) {
                maxSize = QSize(200, 200); // Fallback size
            }
            
            if (pixmap.width() > maxSize.width() || pixmap.height() > maxSize.height()) {
                pixmap = pixmap.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            
            _previewLabel->setPixmap(pixmap);
            _previewLabel->setText("");
        } else {
            _previewLabel->setText("Failed to load FRM");
        }
        
    } catch (const std::exception& e) {
        spdlog::warn("FrmSelectorDialog: Failed to load FRM preview for {}: {}", frmPath.toStdString(), e.what());
        _previewLabel->setText("Preview error");
    }
}

void FrmSelectorDialog::filterFrmList(const QString& searchText) {
    // Function to recursively filter tree items
    std::function<void(QTreeWidgetItem*, const QString&)> filterItem = [&](QTreeWidgetItem* item, const QString& text) {
        if (!item) return;
        
        bool hasVisibleChild = false;
        
        // Check children first
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* child = item->child(i);
            filterItem(child, text);
            if (!child->isHidden()) {
                hasVisibleChild = true;
            }
        }
        
        // Check if this item matches search
        bool matches = text.isEmpty() || 
                      item->text(0).contains(text, Qt::CaseInsensitive);
        
        // Show item if it matches or has visible children
        item->setHidden(!matches && !hasVisibleChild);
    };
    
    // Filter all top-level items
    for (int i = 0; i < _frmTreeWidget->topLevelItemCount(); ++i) {
        filterItem(_frmTreeWidget->topLevelItem(i), searchText);
    }
}

void FrmSelectorDialog::setInitialFrmPid(uint32_t frmPid) {
    _selectedFrmPid = frmPid;
    _frmPidSpin->setValue(static_cast<int>(frmPid));
    
    auto& resourceManager = ResourceManager::getInstance();
    std::string frmPath = resourceManager.FIDtoFrmName(frmPid);
    _frmPathEdit->setText(QString::fromStdString(frmPath));
    
    updatePreview();
    
    // Try to select the corresponding item in the tree by matching FRM path
    std::function<bool(QTreeWidgetItem*)> findAndSelect = [&](QTreeWidgetItem* item) -> bool {
        if (!item) return false;
        
        QString itemPath = item->data(0, Qt::UserRole).toString();
        if (itemPath.toStdString() == frmPath) {
            _frmTreeWidget->setCurrentItem(item);
            _frmTreeWidget->scrollToItem(item);
            return true;
        }
        
        // Check children
        for (int i = 0; i < item->childCount(); ++i) {
            if (findAndSelect(item->child(i))) {
                return true;
            }
        }
        return false;
    };
    
    // Search all top-level items
    for (int i = 0; i < _frmTreeWidget->topLevelItemCount(); ++i) {
        if (findAndSelect(_frmTreeWidget->topLevelItem(i))) {
            break;
        }
    }
}

void FrmSelectorDialog::onSearchTextChanged() {
    filterFrmList(_searchEdit->text());
}

void FrmSelectorDialog::onFrmListSelectionChanged() {
    auto* currentItem = _frmTreeWidget->currentItem();
    if (currentItem) {
        QString selectedPath = currentItem->data(0, Qt::UserRole).toString();
        std::string frmPath = selectedPath.toStdString();
        
        // Skip group headers or category nodes
        if (selectedPath.isEmpty() || selectedPath == "__GROUP_HEADER__") {
            _selectedFrmPid = 0;
            _frmPidSpin->setValue(0);
            _frmPathEdit->clear();
            updatePreview();
            _okButton->setEnabled(false);
            return;
        }
        
        // Try to derive FRM PID from path
        _selectedFrmPid = deriveFrmPidFromPath(frmPath);
        
        _frmPidSpin->setValue(static_cast<int>(_selectedFrmPid));
        _frmPathEdit->setText(selectedPath);
        
        updatePreview();
        _okButton->setEnabled(true);
    } else {
        _selectedFrmPid = 0;
        _frmPidSpin->setValue(0);
        _frmPathEdit->clear();
        updatePreview();
        _okButton->setEnabled(false);
    }
}

void FrmSelectorDialog::onFrmPidChanged() {
    // For now, PID spin box is read-only, so this won't be called by user input
    // But if we make it editable later, we could implement PID-based selection here
}

void FrmSelectorDialog::onAccepted() {
    accept();
}

void FrmSelectorDialog::onRejected() {
    reject();
}

uint32_t FrmSelectorDialog::deriveFrmPidFromPath(const std::string& frmPath) {
    // Input validation
    if (frmPath.empty()) {
        spdlog::debug("FrmSelectorDialog: Empty FRM path provided");
        return 0;
    }
    
    // Normalize path (handle both /art/critters/file.frm and art/critters/file.frm)
    std::string normalizedPath = frmPath;
    if (!normalizedPath.empty() && normalizedPath.front() == '/') {
        normalizedPath = normalizedPath.substr(1); // Remove leading slash
    }
    
    // Handle backslashes on Windows
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    
    // Extract filename from path
    size_t lastSlash = normalizedPath.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? normalizedPath.substr(lastSlash + 1) : normalizedPath;
    
    // Validate filename has proper extension
    if (filename.length() < 5 || 
        (filename.substr(filename.length() - 4) != ".frm" && 
         filename.substr(filename.length() - 4) != ".fr0" &&
         filename.substr(filename.length() - 4) != ".fr1" &&
         filename.substr(filename.length() - 4) != ".fr2" &&
         filename.substr(filename.length() - 4) != ".fr3" &&
         filename.substr(filename.length() - 4) != ".fr4" &&
         filename.substr(filename.length() - 4) != ".fr5")) {
        spdlog::debug("FrmSelectorDialog: Invalid FRM file extension: {}", filename);
        return 0;
    }
    
    // Handle well-known special cases first (use normalized path)
    if (normalizedPath == "art/misc/scrblk.frm") {
        return 0x00000001; // Scroll blocker
    }
    if (normalizedPath == "art/misc/wallblock.frm") {
        return 0x0400026C; // Wall blocker (type=WALL, baseId=620)  
    }
    if (normalizedPath == "art/misc/light.frm") {
        return 0x02000015; // Light source
    }
    
    // Map path prefixes to FRM types and LST files (matching ResourceManager)
    struct FrmTypeInfo {
        std::string pathPrefix;
        std::string lstFile;
        uint32_t frmType;
    };
    
    static const FrmTypeInfo frmTypeMap[] = {
        { "art/items/", "art/items/items.lst", 0 },       // ITEMS
        { "art/critters/", "art/critters/critters.lst", 1 }, // CRITTER
        { "art/scenery/", "art/scenery/scenery.lst", 2 },    // SCENERY  
        { "art/walls/", "art/walls/walls.lst", 4 },          // WALL
        { "art/tiles/", "art/tiles/tiles.lst", 5 },          // TILE
        { "art/misc/", "art/misc/misc.lst", 6 },             // MISC
        { "art/intrface/", "art/intrface/intrface.lst", 7 }, // INTRFACE
        { "art/inven/", "art/inven/inven.lst", 8 },          // INVENTORY
    };
    
    // Find matching type for the path (use normalized path without leading slash)
    const FrmTypeInfo* typeInfo = nullptr;
    for (const auto& info : frmTypeMap) {
        if (normalizedPath.find(info.pathPrefix) == 0) {
            typeInfo = &info;
            break;
        }
    }
    
    if (!typeInfo) {
        spdlog::debug("FrmSelectorDialog: Unknown path type for: {} (normalized: {})", frmPath, normalizedPath);
        return 0;
    }
    
    try {
        auto& resourceManager = ResourceManager::getInstance();
        auto lst = resourceManager.getResource<Lst>(typeInfo->lstFile);
        
        if (!lst) {
            spdlog::warn("FrmSelectorDialog: Failed to load LST file: {}", typeInfo->lstFile);
            return 0;
        }
        
        const auto& fileList = lst->list();
        
        for (size_t i = 0; i < fileList.size(); ++i) {
            std::string lstEntry = fileList[i];
            
            // Skip empty entries
            if (lstEntry.empty()) {
                continue;
            }
            
            if (typeInfo->frmType == 1) { // CRITTER type
                size_t commaPos = lstEntry.find(',');
                std::string baseName = (commaPos != std::string::npos) ? lstEntry.substr(0, commaPos) : lstEntry;
                
                baseName.erase(0, baseName.find_first_not_of(" \t\r\n"));
                baseName.erase(baseName.find_last_not_of(" \t\r\n") + 1);
                
                if (baseName.empty()) {
                    continue;
                }
                
                if (CritterFrmResolver::matchesCritterBase(baseName, filename)) {
                    uint32_t fid = CritterFrmResolver::deriveCritterFrmPid(baseName, filename, static_cast<uint32_t>(i));
                    if (fid != 0) {
                        return fid;
                    }
                }
            } else {
                std::string trimmedEntry = lstEntry;
                trimmedEntry.erase(0, trimmedEntry.find_first_not_of(" \t\r\n"));
                trimmedEntry.erase(trimmedEntry.find_last_not_of(" \t\r\n") + 1);
                
                if (trimmedEntry.empty()) {
                    continue;
                }
                
                if (trimmedEntry == filename) {
                    return (typeInfo->frmType << 24) | static_cast<uint32_t>(i);
                }
                
                std::string lowerEntry = trimmedEntry;
                std::string lowerFilename = filename;
                std::transform(lowerEntry.begin(), lowerEntry.end(), lowerEntry.begin(), ::tolower);
                std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
                
                if (lowerEntry == lowerFilename) {
                    return (typeInfo->frmType << 24) | static_cast<uint32_t>(i);
                }
            }
        }
        
        uint32_t fallbackFid = tryFallbackFidDerivation(normalizedPath, filename, typeInfo->frmType);
        if (fallbackFid != 0) {
            return fallbackFid;
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        spdlog::error("FrmSelectorDialog: Error deriving FID for {}: {}", frmPath, e.what());
        return 0;
    }
}

uint32_t FrmSelectorDialog::tryFallbackFidDerivation(const std::string& /* normalizedPath */, 
                                                    const std::string& filename, 
                                                    uint32_t frmType) {
    if (frmType == 0) { // ITEMS
        std::string baseName = filename;
        size_t dotPos = baseName.find_last_of('.');
        if (dotPos != std::string::npos) {
            baseName = baseName.substr(0, dotPos);
        }
        
        std::regex numPattern("^([a-zA-Z_]+?)_?(\\d+)$");
        std::smatch match;
        if (std::regex_match(baseName, match, numPattern)) {
            uint32_t index = std::stoul(match[2].str());
            return (frmType << 24) | index;
        }
    }
    
    if (frmType == 1) { // Critters
        return (frmType << 24) | 0x00FF0000;
    }
    
    return 0;
}

std::string FrmSelectorDialog::getGroupingKey(const std::string& frmPath) {
    // Extract path prefix to determine type
    std::string normalizedPath = frmPath;
    if (!normalizedPath.empty() && normalizedPath.front() == '/') {
        normalizedPath = normalizedPath.substr(1);
    }
    
    // For critters, group by base name
    if (normalizedPath.find("art/critters/") == 0) {
        // Extract filename
        size_t lastSlash = normalizedPath.find_last_of('/');
        std::string filename = (lastSlash != std::string::npos) ? normalizedPath.substr(lastSlash + 1) : normalizedPath;
        
        // Remove extension
        size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos) {
            filename = filename.substr(0, dotPos);
        }
        
        // Extract base name (first 6 characters for critters)
        if (filename.length() >= 8) { // 6 char base + 2 char suffix minimum
            return "Critter: " + filename.substr(0, 6);
        }
    }
    
    // For other types, group by directory
    size_t secondSlash = normalizedPath.find('/', normalizedPath.find('/') + 1);
    if (secondSlash != std::string::npos) {
        std::string typeDir = normalizedPath.substr(0, secondSlash);
        if (typeDir == "art/items") return "Items";
        if (typeDir == "art/scenery") return "Scenery";
        if (typeDir == "art/walls") return "Walls";
        if (typeDir == "art/tiles") return "Tiles";
        if (typeDir == "art/misc") return "Miscellaneous";
        if (typeDir == "art/intrface") return "Interface";
        if (typeDir == "art/inven") return "Inventory";
    }
    
    return "Other";
}

bool FrmSelectorDialog::isCritterGroup(const std::string& groupName) {
    return groupName.find("Critter:") == 0;
}

std::string FrmSelectorDialog::getAnimationSortKey(const std::string& frmPath) {
    // Extract filename
    size_t lastSlash = frmPath.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? frmPath.substr(lastSlash + 1) : frmPath;
    
    // For critters, prioritize by animation type
    if (frmPath.find("art/critters/") != std::string::npos) {
        // Extract animation suffix (simplified parsing)
        if (filename.length() >= 8) { // 6 char base + 2 char suffix minimum
            std::string suffix = filename.substr(filename.length() - 2); // Last 2 characters
            // Convert to lowercase for consistent matching
            std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
            
            // Priority animations come first
            static const std::map<std::string, int> animationPriority = {
                {"aa", 1},  // Standing (most important)
                {"ab", 2},  // Walking
                {"ad", 3},  // Running
                {"ae", 4},  // Sneaking
                {"af", 5},  // Single Attack
                {"ag", 6},  // Burst Attack
                {"ba", 7},  // Weapon Idle
                {"bb", 8},  // Weapon Walk
                {"bf", 9},  // Weapon Single
                {"bg", 10}, // Weapon Burst
                {"ak", 11}, // Damage
                {"al", 12}, // Dead Front
                {"an", 13}, // Dead Back
            };
            
            auto it = animationPriority.find(suffix);
            int priority = (it != animationPriority.end()) ? it->second : 999;
            
            // Sort by priority, then direction, then suffix
            return std::to_string(priority) + "_" + suffix;
        }
    }
    
    // Default alphabetical sort
    return filename;
}

QString FrmSelectorDialog::createDisplayName(const std::string& frmPath) {
    // Extract filename
    size_t lastSlash = frmPath.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? frmPath.substr(lastSlash + 1) : frmPath;
    
    // For critters, add animation description
    if (frmPath.find("art/critters/") != std::string::npos) {
        std::string animationType = CritterFrmResolver::getAnimationTypeName(filename);
        if (animationType != "Unknown") {
            return QString("%1 - %2").arg(QString::fromStdString(filename), QString::fromStdString(animationType));
        }
    }
    
    return QString::fromStdString(filename);
}

void FrmSelectorDialog::setObjectTypeFilter(uint32_t objectType) {
    _objectTypeFilter = objectType;
    // Repopulate the list with the new filter
    populateFrmList();
}

} // namespace geck