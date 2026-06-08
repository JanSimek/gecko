#include "FrmSelectorDialog.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"

#include <QApplication>
#include <QPixmap>
#include <spdlog/spdlog.h>
#include <regex>
#include <algorithm>
#include <functional>

#include "../../resource/GameResources.h"
#include "../../resource/FrmResolver.h"
#include "../../util/CritterFrmResolver.h"
#include "../../format/frm/Frm.h"

namespace geck {

FrmSelectorDialog::FrmSelectorDialog(resource::GameResources& resources, QWidget* parent)
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
    , _resources(resources)
    , _selectedFrmPid(std::nullopt)
    , _objectTypeFilter(std::nullopt) {

    setupUI();
    populateFrmList();
}

std::optional<Frm::FRM_TYPE> FrmSelectorDialog::filterForObjectType(Pro::OBJECT_TYPE objectType) {
    switch (objectType) {
        case Pro::OBJECT_TYPE::ITEM:
            return Frm::FRM_TYPE::ITEM;
        case Pro::OBJECT_TYPE::CRITTER:
            return Frm::FRM_TYPE::CRITTER;
        case Pro::OBJECT_TYPE::SCENERY:
            return Frm::FRM_TYPE::SCENERY;
        case Pro::OBJECT_TYPE::WALL:
            return Frm::FRM_TYPE::WALL;
        case Pro::OBJECT_TYPE::TILE:
            return Frm::FRM_TYPE::TILE;
        case Pro::OBJECT_TYPE::MISC:
            return Frm::FRM_TYPE::MISC;
    }

    return std::nullopt;
}

std::optional<Frm::FRM_TYPE> FrmSelectorDialog::filterForFid(uint32_t fid) {
    const auto frmType = static_cast<Frm::FRM_TYPE>((fid >> 24) & 0xFF);
    if (frmType > Frm::FRM_TYPE::MISC) {
        return std::nullopt;
    }

    return frmType;
}

void FrmSelectorDialog::setupUI() {
    setWindowTitle("Select FRM File");
    setModal(true);
    resize(ui::constants::dialog_sizes::LARGE_WIDTH, ui::constants::dialog_sizes::LARGE_HEIGHT);

    _mainLayout = new QVBoxLayout(this);

    _splitter = new QSplitter(Qt::Horizontal, this);

    // Left panel - FRM list
    _listPanel = new QWidget();
    _listLayout = new QVBoxLayout(_listPanel);

    _searchEdit = new QLineEdit();
    _searchEdit->setPlaceholderText("Search FRM files...");
    connect(_searchEdit, &QLineEdit::textChanged, this, &FrmSelectorDialog::onSearchTextChanged);
    _listLayout->addWidget(_searchEdit);

    _frmTreeWidget = new QTreeWidget();
    _frmTreeWidget->setHeaderLabel("FRM Files");
    _frmTreeWidget->setAlternatingRowColors(true);
    connect(_frmTreeWidget, &QTreeWidget::itemSelectionChanged, this, &FrmSelectorDialog::onFrmListSelectionChanged);
    _listLayout->addWidget(_frmTreeWidget);

    // Right panel - Preview and details
    _previewPanel = new QWidget();
    _previewLayout = new QVBoxLayout(_previewPanel);

    _previewGroup = new QGroupBox("Preview");
    QVBoxLayout* previewGroupLayout = new QVBoxLayout(_previewGroup);

    _previewLabel = new QLabel("No FRM selected");
    _previewLabel->setAlignment(Qt::AlignCenter);
    _previewLabel->setMinimumHeight(ui::constants::sizes::PREVIEW_LARGE);
    _previewLabel->setMinimumWidth(ui::constants::sizes::PREVIEW_LARGE);
    _previewLabel->setScaledContents(false);
    _previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _previewLabel->setStyleSheet(ui::theme::styles::previewArea());
    previewGroupLayout->addWidget(_previewLabel);

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
    _splitter->setSizes({ 480, 320 });

    _mainLayout->addWidget(_splitter);

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
        auto frmFiles = _resources.files().list("*.frm");

        _frmFiles.clear();
        _frmTreeWidget->clear();

        // Group files by type and base name for tree organization
        std::map<std::string, std::vector<std::string>> groupedFiles;

        for (const auto& frmPath : frmFiles) {
            const std::string frmPathString = frmPath.generic_string();

            const auto frmType = resource::frmTypeForArtPath(frmPathString);

            if (_objectTypeFilter.has_value() && frmType != _objectTypeFilter) {
                continue;
            }

            // Without a filter, keep anything under a known art/ directory.
            if (!_objectTypeFilter.has_value() && !frmType.has_value()) {
                continue;
            }

            _frmFiles.emplace_back(0, frmPathString); // PID resolved dynamically when needed

            std::string groupKey = getGroupingKey(frmPathString);
            groupedFiles[groupKey].push_back(frmPathString);
        }

        QTreeWidgetItem* itemsRoot = nullptr;
        QTreeWidgetItem* crittersRoot = nullptr;
        QTreeWidgetItem* sceneryRoot = nullptr;
        QTreeWidgetItem* wallsRoot = nullptr;
        QTreeWidgetItem* tilesRoot = nullptr;
        QTreeWidgetItem* miscRoot = nullptr;

        // FRM type -> its lazily-created root node and display label. Types without
        // an entry here (interface, inventory) get no root and stay unparented.
        const struct {
            Frm::FRM_TYPE type;
            QTreeWidgetItem** root;
            const char* label;
        } categories[] = {
            { Frm::FRM_TYPE::CRITTER, &crittersRoot, "Critters" },
            { Frm::FRM_TYPE::ITEM, &itemsRoot, "Items" },
            { Frm::FRM_TYPE::SCENERY, &sceneryRoot, "Scenery" },
            { Frm::FRM_TYPE::WALL, &wallsRoot, "Walls" },
            { Frm::FRM_TYPE::TILE, &tilesRoot, "Tiles" },
            { Frm::FRM_TYPE::MISC, &miscRoot, "Misc" },
        };

        for (const auto& group : groupedFiles) {
            const std::string& groupName = group.first;
            const auto& files = group.second;

            // Determine which root category this group belongs to
            QTreeWidgetItem* parentItem = nullptr;
            if (!files.empty()) {
                const auto type = resource::frmTypeForArtPath(files.front());
                for (const auto& category : categories) {
                    if (type != category.type) {
                        continue;
                    }
                    if (!*category.root) {
                        *category.root = new QTreeWidgetItem(_frmTreeWidget);
                        (*category.root)->setText(0, category.label);
                        (*category.root)->setExpanded(true);
                    }
                    parentItem = *category.root;
                    break;
                }
            }

            // Critter groups with multiple files get a collapsible header node
            QTreeWidgetItem* groupNode = nullptr;
            if (files.size() > 1 && isCritterGroup(groupName) && parentItem) {
                groupNode = new QTreeWidgetItem(parentItem);
                groupNode->setText(0, QString::fromStdString(groupName));
                groupNode->setData(0, Qt::UserRole, QString("__GROUP_HEADER__"));
            }

            std::vector<std::string> sortedFiles = files;
            std::sort(sortedFiles.begin(), sortedFiles.end(), [this](const std::string& a, const std::string& b) {
                return getAnimationSortKey(a) < getAnimationSortKey(b);
            });

            for (const auto& frmPath : sortedFiles) {
                QString displayName = createDisplayName(frmPath);

                QTreeWidgetItem* targetParent = groupNode ? groupNode : parentItem;
                if (!targetParent) {
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
    QString frmPath = _frmPathEdit->text();

    if (frmPath.isEmpty()) {
        _previewLabel->clear();
        _previewLabel->setText("No FRM selected");
        return;
    }

    try {
        std::string frmPathStr = frmPath.toStdString();

        const auto& texture = _resources.textures().get(frmPathStr);

        auto image = texture.copyToImage();
        const std::uint8_t* pixels = image.getPixelsPtr();
        QImage qImage(pixels, image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);

        try {
            auto frm = _resources.repository().load<Frm>(frmPathStr);
            if (frm && !frm->directions().empty() && !frm->directions()[0].frames().empty()) {
                // Multi-frame FRM (e.g. critters): extract only the first frame of direction 0
                const auto& firstFrame = frm->directions()[0].frames()[0];

                uint16_t left = 0;
                uint16_t top = 0;
                uint16_t width = firstFrame.width();
                uint16_t height = firstFrame.height();

                if (left + width <= qImage.width() && top + height <= qImage.height()) {
                    qImage = qImage.copy(left, top, width, height);
                }

                spdlog::debug("FrmSelectorDialog: Extracted first frame {}x{} from FRM: {}",
                    width, height, frmPathStr);
            }
        } catch (const std::exception& e) {
            // Fall back to the full texture for simple FRMs
            spdlog::debug("FrmSelectorDialog: Using full texture for FRM preview ({}): {}",
                frmPathStr, e.what());
        }

        QPixmap pixmap = QPixmap::fromImage(qImage);
        if (!pixmap.isNull()) {
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
    std::function<void(QTreeWidgetItem*, const QString&)> filterItem = [&](QTreeWidgetItem* item, const QString& text) {
        if (!item)
            return;

        bool hasVisibleChild = false;

        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* child = item->child(i);
            filterItem(child, text);
            if (!child->isHidden()) {
                hasVisibleChild = true;
            }
        }

        bool matches = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);

        // Keep an item visible if it matches or has any visible child
        item->setHidden(!matches && !hasVisibleChild);
    };

    for (int i = 0; i < _frmTreeWidget->topLevelItemCount(); ++i) {
        filterItem(_frmTreeWidget->topLevelItem(i), searchText);
    }
}

void FrmSelectorDialog::setInitialFrmPid(uint32_t frmPid) {
    _selectedFrmPid = frmPid;
    _frmPidSpin->setValue(static_cast<int>(frmPid));

    std::string frmPath = _resources.frmResolver().resolve(frmPid);
    _frmPathEdit->setText(QString::fromStdString(frmPath));

    updatePreview();

    // Select the tree item whose stored path matches frmPath
    std::function<bool(QTreeWidgetItem*)> findAndSelect = [&](QTreeWidgetItem* item) -> bool {
        if (!item)
            return false;

        QString itemPath = item->data(0, Qt::UserRole).toString();
        if (itemPath.toStdString() == frmPath) {
            _frmTreeWidget->setCurrentItem(item);
            _frmTreeWidget->scrollToItem(item);
            return true;
        }

        for (int i = 0; i < item->childCount(); ++i) {
            if (findAndSelect(item->child(i))) {
                return true;
            }
        }
        return false;
    };

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

        // Skip group headers and category nodes
        if (selectedPath.isEmpty() || selectedPath == "__GROUP_HEADER__") {
            _selectedFrmPid = std::nullopt;
            _frmPidSpin->setValue(0);
            _frmPathEdit->clear();
            updatePreview();
            _okButton->setEnabled(false);
            return;
        }

        _selectedFrmPid = deriveFrmPidFromPath(frmPath);

        _frmPidSpin->setValue(static_cast<int>(_selectedFrmPid.value_or(0)));
        _frmPathEdit->setText(selectedPath);

        updatePreview();
        _okButton->setEnabled(true);
    } else {
        _selectedFrmPid = std::nullopt;
        _frmPidSpin->setValue(0);
        _frmPathEdit->clear();
        updatePreview();
        _okButton->setEnabled(false);
    }
}

void FrmSelectorDialog::onFrmPidChanged() {
    // PID spin box is read-only, so user input never reaches here. If made editable,
    // implement PID-based selection here.
}

void FrmSelectorDialog::onAccepted() {
    accept();
}

void FrmSelectorDialog::onRejected() {
    reject();
}

std::optional<uint32_t> FrmSelectorDialog::deriveFrmPidFromPath(const std::string& frmPath) {
    if (frmPath.empty()) {
        spdlog::debug("FrmSelectorDialog: Empty FRM path provided");
        return std::nullopt;
    }

    // Normalize path (handle both /art/critters/file.frm and art/critters/file.frm)
    std::string normalizedPath = frmPath;
    if (!normalizedPath.empty() && normalizedPath.front() == '/') {
        normalizedPath = normalizedPath.substr(1);
    }

    // Handle Windows backslashes
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');

    size_t lastSlash = normalizedPath.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? normalizedPath.substr(lastSlash + 1) : normalizedPath;

    // Validate filename has proper extension
    if (filename.length() < 5 || (filename.substr(filename.length() - 4) != ".frm" && filename.substr(filename.length() - 4) != ".fr0" && filename.substr(filename.length() - 4) != ".fr1" && filename.substr(filename.length() - 4) != ".fr2" && filename.substr(filename.length() - 4) != ".fr3" && filename.substr(filename.length() - 4) != ".fr4" && filename.substr(filename.length() - 4) != ".fr5")) {
        spdlog::debug("FrmSelectorDialog: Invalid FRM file extension: {}", filename);
        return std::nullopt;
    }

    // Handle well-known special cases first (use normalized path).
    // These are legitimate (sometimes low-valued) FIDs, not failure sentinels.
    if (normalizedPath == "art/misc/scrblk.frm") {
        return uint32_t{ 0x00000001 }; // Scroll blocker
    }
    if (normalizedPath == "art/misc/wallblock.frm") {
        return uint32_t{ 0x0400026C }; // Wall blocker (type=WALL, baseId=620)
    }
    if (normalizedPath == "art/misc/light.frm") {
        return uint32_t{ 0x02000015 }; // Light source
    }

    // Canonical LST-based derivation lives in the resource layer (engine-correct
    // FID type byte, shared with FrmResolver::resolve).
    try {
        if (const auto fid = _resources.frmResolver().resolveFid(frmPath)) {
            return fid;
        }

        // The FRM sits under a known art directory but is absent from its LST:
        // fall back to the editor's heuristic derivation for items and critters.
        if (const auto type = resource::frmTypeForArtPath(normalizedPath)) {
            const uint32_t fallbackFid = tryFallbackFidDerivation(normalizedPath, filename, static_cast<uint32_t>(*type));
            if (fallbackFid != 0) {
                return fallbackFid;
            }
        }

        return std::nullopt;

    } catch (const std::exception& e) {
        spdlog::error("FrmSelectorDialog: Error deriving FID for {}: {}", frmPath, e.what());
        return std::nullopt;
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
    std::string normalizedPath = frmPath;
    if (!normalizedPath.empty() && normalizedPath.front() == '/') {
        normalizedPath = normalizedPath.substr(1);
    }

    // Critters group by base name (first 6 chars of the filename)
    if (normalizedPath.find("art/critters/") == 0) {
        size_t lastSlash = normalizedPath.find_last_of('/');
        std::string filename = (lastSlash != std::string::npos) ? normalizedPath.substr(lastSlash + 1) : normalizedPath;

        size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos) {
            filename = filename.substr(0, dotPos);
        }

        if (filename.length() >= 8) { // 6 char base + 2 char suffix minimum
            return "Critter: " + filename.substr(0, 6);
        }
    }

    // Other types group by directory
    size_t secondSlash = normalizedPath.find('/', normalizedPath.find('/') + 1);
    if (secondSlash != std::string::npos) {
        std::string typeDir = normalizedPath.substr(0, secondSlash);
        if (typeDir == "art/items")
            return "Items";
        if (typeDir == "art/scenery")
            return "Scenery";
        if (typeDir == "art/walls")
            return "Walls";
        if (typeDir == "art/tiles")
            return "Tiles";
        if (typeDir == "art/misc")
            return "Miscellaneous";
        if (typeDir == "art/intrface")
            return "Interface";
        if (typeDir == "art/inven")
            return "Inventory";
    }

    return "Other";
}

bool FrmSelectorDialog::isCritterGroup(const std::string& groupName) {
    return groupName.find("Critter:") == 0;
}

std::string FrmSelectorDialog::getAnimationSortKey(const std::string& frmPath) {
    size_t lastSlash = frmPath.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? frmPath.substr(lastSlash + 1) : frmPath;

    // Critters are sorted by animation-type priority (the 2-char filename suffix)
    if (frmPath.find("art/critters/") != std::string::npos) {
        if (filename.length() >= 8) {                                    // 6 char base + 2 char suffix minimum
            std::string suffix = filename.substr(filename.length() - 2); // Last 2 characters
            std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);

            static const std::map<std::string, int> animationPriority = {
                { "aa", 1 },  // Standing (most important)
                { "ab", 2 },  // Walking
                { "ad", 3 },  // Running
                { "ae", 4 },  // Sneaking
                { "af", 5 },  // Single Attack
                { "ag", 6 },  // Burst Attack
                { "ba", 7 },  // Weapon Idle
                { "bb", 8 },  // Weapon Walk
                { "bf", 9 },  // Weapon Single
                { "bg", 10 }, // Weapon Burst
                { "ak", 11 }, // Damage
                { "al", 12 }, // Dead Front
                { "an", 13 }, // Dead Back
            };

            auto it = animationPriority.find(suffix);
            int priority = (it != animationPriority.end()) ? it->second : 999;

            return std::to_string(priority) + "_" + suffix;
        }
    }

    return filename; // default alphabetical sort
}

QString FrmSelectorDialog::createDisplayName(const std::string& frmPath) {
    size_t lastSlash = frmPath.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? frmPath.substr(lastSlash + 1) : frmPath;

    // Critters get an animation-type description appended
    if (frmPath.find("art/critters/") != std::string::npos) {
        std::string animationType = CritterFrmResolver::getAnimationTypeName(filename);
        if (animationType != "Unknown") {
            return QString("%1 - %2").arg(QString::fromStdString(filename), QString::fromStdString(animationType));
        }
    }

    return QString::fromStdString(filename);
}

void FrmSelectorDialog::setObjectTypeFilter(std::optional<Frm::FRM_TYPE> objectType) {
    _objectTypeFilter = objectType;
    populateFrmList();
}

} // namespace geck
