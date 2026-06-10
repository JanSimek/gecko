#include "ScriptSelectorDialog.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace geck {

ScriptSelectorDialog::ScriptSelectorDialog(const std::vector<std::string>& scriptNames,
    int currentIndex, QWidget* parent)
    : QDialog(parent) {

    setWindowTitle("Select Script");
    resize(360, 460);

    auto* mainLayout = new QVBoxLayout(this);

    _filterEdit = new QLineEdit(this);
    _filterEdit->setPlaceholderText("Filter scripts...");
    mainLayout->addWidget(_filterEdit);

    _listWidget = new QListWidget(this);
    for (size_t i = 0; i < scriptNames.size(); ++i) {
        // Show "index: name" but carry the program index in the item's data.
        auto* item = new QListWidgetItem(
            QString("%1: %2").arg(i).arg(QString::fromStdString(scriptNames[i])), _listWidget);
        item->setData(Qt::UserRole, static_cast<int>(i));
    }
    mainLayout->addWidget(_listWidget);

    if (currentIndex >= 0 && currentIndex < _listWidget->count()) {
        _listWidget->setCurrentRow(currentIndex);
    }

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_listWidget, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(_filterEdit, &QLineEdit::textChanged, this, &ScriptSelectorDialog::onFilterChanged);
    mainLayout->addWidget(buttonBox);
}

void ScriptSelectorDialog::onFilterChanged(const QString& text) {
    for (int i = 0; i < _listWidget->count(); ++i) {
        auto* item = _listWidget->item(i);
        item->setHidden(!text.isEmpty() && !item->text().contains(text, Qt::CaseInsensitive));
    }
}

int ScriptSelectorDialog::selectedIndex() const {
    auto* item = _listWidget->currentItem();
    if (!item || item->isHidden()) {
        return -1;
    }
    return item->data(Qt::UserRole).toInt();
}

} // namespace geck
