#include "ScriptSelectorDialog.h"

#include "format/lst/Lst.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptNames.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>

namespace geck {

namespace {
    enum Column {
        COL_INDEX = 0,
        COL_FILENAME = 1,
        COL_NAME = 2,
    };
} // namespace

std::vector<ScriptSelectorDialog::Entry> ScriptSelectorDialog::buildEntries(resource::GameResources& resources) {
    std::vector<Entry> entries;
    const Lst* lst = resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
    if (lst == nullptr) {
        return entries;
    }
    const auto& names = lst->list();
    entries.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        entries.push_back({ static_cast<int>(i), names[i], resource::scriptDisplayName(resources, static_cast<int>(i)) });
    }
    return entries;
}

ScriptSelectorDialog::ScriptSelectorDialog(const std::vector<Entry>& scripts, int currentIndex, QWidget* parent)
    : BaseDialog("Select Script", parent) {

    resize(560, 480);

    auto* mainLayout = new QVBoxLayout(this);

    _filterEdit = new QLineEdit(this);
    _filterEdit->setPlaceholderText("Filter scripts...");
    mainLayout->addWidget(_filterEdit);

    _table = new QTableWidget(static_cast<int>(scripts.size()), 3, this);
    _table->setHorizontalHeaderLabels({ "Index", "Filename", "Name" });
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->verticalHeader()->setVisible(false);
    _table->horizontalHeader()->setSectionResizeMode(COL_INDEX, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(COL_FILENAME, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);

    for (int row = 0; row < static_cast<int>(scripts.size()); ++row) {
        const Entry& entry = scripts[static_cast<size_t>(row)];

        // The index cell carries the program index as an int (numeric display + sort), and selectedIndex
        // reads it back — so the result is correct regardless of column sorting.
        auto* indexItem = new QTableWidgetItem;
        indexItem->setData(Qt::DisplayRole, entry.index);
        _table->setItem(row, COL_INDEX, indexItem);
        _table->setItem(row, COL_FILENAME, new QTableWidgetItem(QString::fromStdString(entry.filename)));
        _table->setItem(row, COL_NAME, new QTableWidgetItem(QString::fromStdString(entry.name)));
    }
    _table->setSortingEnabled(true);
    _table->sortByColumn(COL_INDEX, Qt::AscendingOrder); // sensible default; the user can re-sort by any column

    if (currentIndex >= 0) {
        for (int row = 0; row < _table->rowCount(); ++row) {
            const QTableWidgetItem* item = _table->item(row, COL_INDEX);
            if (item != nullptr && item->data(Qt::DisplayRole).toInt() == currentIndex) {
                _table->selectRow(row);
                _table->scrollToItem(item);
                break;
            }
        }
    }
    mainLayout->addWidget(_table);

    auto* buttonBox = createButtonBox();
    connect(_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { accept(); });
    connect(_filterEdit, &QLineEdit::textChanged, this, &ScriptSelectorDialog::onFilterChanged);
    mainLayout->addWidget(buttonBox);
}

void ScriptSelectorDialog::onFilterChanged(const QString& text) {
    for (int row = 0; row < _table->rowCount(); ++row) {
        bool match = text.isEmpty();
        for (int col = 0; !match && col < _table->columnCount(); ++col) {
            const QTableWidgetItem* item = _table->item(row, col);
            if (item != nullptr && item->text().contains(text, Qt::CaseInsensitive)) {
                match = true;
            }
        }
        _table->setRowHidden(row, !match);
    }
}

int ScriptSelectorDialog::selectedIndex() const {
    const int row = _table->currentRow();
    if (row < 0 || _table->isRowHidden(row)) {
        return -1;
    }
    const QTableWidgetItem* item = _table->item(row, COL_INDEX);
    return item != nullptr ? item->data(Qt::DisplayRole).toInt() : -1;
}

} // namespace geck
