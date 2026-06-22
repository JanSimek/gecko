#include "ui/dialogs/ItemSelectorDialog.h"

#include "format/lst/Lst.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "ui/common/InventoryItemUiHelper.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include <cstddef>
#include <string>

namespace geck {

namespace {
    constexpr int PREVIEW_ICON_SIZE = 96;
    constexpr int PID_ROLE = Qt::UserRole + 1;

    enum Column {
        COL_NAME = 0,
        COL_TYPE = 1,
        COL_PID = 2,
    };
} // namespace

ItemSelectorDialog::ItemSelectorDialog(resource::GameResources& resources, QWidget* parent)
    : QDialog(parent)
    , _resources(resources) {
    setupUI();
    populate();
}

int ItemSelectorDialog::selectedAmount() const {
    return _amountSpin->value();
}

void ItemSelectorDialog::setupUI() {
    setWindowTitle("Add Item");
    setModal(true);
    resize(640, 460);

    _search = new QLineEdit(this);
    _search->setPlaceholderText("Filter items by name or PID…");
    _search->setClearButtonEnabled(true);

    _tree = new QTreeWidget(this);
    _tree->setColumnCount(3);
    _tree->setHeaderLabels({ "Name", "Type", "PID" });
    _tree->setRootIsDecorated(false);
    _tree->setAlternatingRowColors(true);
    _tree->setSelectionMode(QAbstractItemView::SingleSelection);
    _tree->setUniformRowHeights(true);
    _tree->header()->setStretchLastSection(false);
    _tree->header()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);
    _tree->header()->setSectionResizeMode(COL_TYPE, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(COL_PID, QHeaderView::ResizeToContents);

    auto* listPanel = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->addWidget(_search);
    listLayout->addWidget(_tree, 1);

    _previewIcon = new QLabel(this);
    _previewIcon->setAlignment(Qt::AlignCenter);
    _previewIcon->setMinimumSize(PREVIEW_ICON_SIZE, PREVIEW_ICON_SIZE);
    _previewName = new QLabel("—", this);
    _previewName->setWordWrap(true);
    _previewType = new QLabel("—", this);
    _previewPid = new QLabel("—", this);

    _amountSpin = new QSpinBox(this);
    _amountSpin->setRange(1, 99999);
    _amountSpin->setValue(1);

    auto* detailsGroup = new QGroupBox("Item", this);
    auto* detailsLayout = new QFormLayout(detailsGroup);
    detailsLayout->addRow(_previewIcon);
    detailsLayout->addRow("Name:", _previewName);
    detailsLayout->addRow("Type:", _previewType);
    detailsLayout->addRow("PID:", _previewPid);
    detailsLayout->addRow("Amount:", _amountSpin);

    auto* previewPanel = new QWidget(this);
    auto* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(detailsGroup);
    previewLayout->addStretch(1);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(listPanel);
    splitter->addWidget(previewPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    _okButton = buttons->button(QDialogButtonBox::Ok);
    _okButton->setText("Add");
    _okButton->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);

    connect(_search, &QLineEdit::textChanged, this, &ItemSelectorDialog::onSearchTextChanged);
    connect(_tree, &QTreeWidget::itemSelectionChanged, this, &ItemSelectorDialog::onSelectionChanged);
    connect(_tree, &QTreeWidget::itemActivated, this, &ItemSelectorDialog::onItemActivated);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ItemSelectorDialog::populate() {
    const Lst* lst = nullptr;
    try {
        lst = _resources.repository().load<Lst>(std::string(ResourcePaths::Lst::PROTO_ITEMS));
    } catch (const std::exception& e) {
        spdlog::warn("ItemSelectorDialog: could not load items.lst: {}", e.what());
    }
    if (lst == nullptr) {
        return;
    }

    // Resolving every item's name + type means loading its proto, so this loop touches every item
    // proto once. Show a wait cursor for the one-time cost (protos are cached, so reopening is fast).
    const auto& files = lst->list();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (std::size_t i = 0; i < files.size(); ++i) {
        // An item PID's low 24 bits are the 1-based items.lst line; the type (ITEM) is the high byte.
        const uint32_t pid = Pro::makePid(Pro::OBJECT_TYPE::ITEM, static_cast<uint32_t>(i + 1));

        const ui::inventory::ItemDetails details = ui::inventory::describeItem(_resources, pid);
        // describeItem yields a placeholder name when a proto can't be resolved; prefer the real .pro
        // filename in that case so the row stays a meaningful identifier.
        const QString name = details.resolved ? details.name : QString::fromStdString(files[i]);

        auto* row = new QTreeWidgetItem(_tree);
        row->setText(COL_NAME, name);
        row->setText(COL_TYPE, details.typeName);
        row->setText(COL_PID, details.pidText);
        row->setData(COL_NAME, PID_ROLE, static_cast<qulonglong>(pid));
    }
    QApplication::restoreOverrideCursor();
    _tree->sortItems(COL_NAME, Qt::AscendingOrder);
}

void ItemSelectorDialog::onSearchTextChanged(const QString& text) const {
    const QString needle = text.trimmed();
    for (int i = 0; i < _tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = _tree->topLevelItem(i);
        const auto pid = static_cast<uint32_t>(item->data(COL_NAME, PID_ROLE).toULongLong());
        // Match the name, the displayed hex PID, and the decimal PID — the old prompt accepted a PID
        // in either base, so a pasted hex or decimal value should still find the item.
        const bool match = needle.isEmpty()
            || item->text(COL_NAME).contains(needle, Qt::CaseInsensitive)
            || item->text(COL_PID).contains(needle, Qt::CaseInsensitive)
            || QString::number(pid).contains(needle);
        item->setHidden(!match);
    }
}

void ItemSelectorDialog::onSelectionChanged() {
    const QList<QTreeWidgetItem*> selection = _tree->selectedItems();
    if (selection.isEmpty()) {
        _selectedPid.reset();
        _okButton->setEnabled(false);
        updatePreview(nullptr);
        return;
    }
    const QTreeWidgetItem* item = selection.first();
    _selectedPid = static_cast<uint32_t>(item->data(COL_NAME, PID_ROLE).toULongLong());
    _okButton->setEnabled(true);
    updatePreview(item);
}

void ItemSelectorDialog::onItemActivated(const QTreeWidgetItem* item, int /*column*/) {
    if (item == nullptr) {
        return;
    }
    _selectedPid = static_cast<uint32_t>(item->data(COL_NAME, PID_ROLE).toULongLong());
    accept(); // double-click / Enter picks the item
}

void ItemSelectorDialog::updatePreview(const QTreeWidgetItem* item) {
    if (item == nullptr) {
        _previewIcon->clear();
        _previewName->setText("—");
        _previewType->setText("—");
        _previewPid->setText("—");
        return;
    }

    _previewName->setText(item->text(COL_NAME));
    _previewType->setText(item->text(COL_TYPE));
    _previewPid->setText(item->text(COL_PID));

    const auto pid = static_cast<uint32_t>(item->data(COL_NAME, PID_ROLE).toULongLong());
    QPixmap icon;
    try {
        icon = ui::inventory::loadItemIcon(_resources, pid, PREVIEW_ICON_SIZE, /*fixedCanvas=*/true);
    } catch (const std::exception& e) {
        spdlog::debug("ItemSelectorDialog: no icon for PID {}: {}", pid, e.what());
    }
    if (!icon.isNull()) {
        _previewIcon->setPixmap(icon);
    } else {
        _previewIcon->clear();
    }
}

} // namespace geck
