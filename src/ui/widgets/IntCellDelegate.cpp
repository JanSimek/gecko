#include "IntCellDelegate.h"

#include <QIntValidator>
#include <QLineEdit>

#include <limits>

namespace geck {

IntCellDelegate::IntCellDelegate(int editableColumn, QObject* parent)
    : QStyledItemDelegate(parent)
    , _editableColumn(editableColumn) {
}

QWidget* IntCellDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& /*option*/,
    const QModelIndex& index) const {
    // Only the one configured column may be edited. Returning nullptr for any other column gates it
    // even when its item still carries Qt::ItemIsEditable (item flags are per-row, not per-column).
    if (index.column() != _editableColumn) {
        return nullptr;
    }

    auto* editor = new QLineEdit(parent);
    // Map variable values are signed 32-bit: accept the full int range, negatives included.
    auto* validator = new QIntValidator(std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(), editor);
    editor->setValidator(validator);
    return editor;
}

} // namespace geck
