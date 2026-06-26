#pragma once

#include <QStyledItemDelegate>

namespace geck {

/// @brief Item delegate that makes exactly one column editable as a signed 32-bit integer.
///
/// Set on a QTreeWidget / QTableWidget view so the editable column accepts a full-range
/// int (negatives included) via a QLineEdit + QIntValidator, while every other column has
/// no editor (createEditor returns nullptr) and so cannot be edited even if its item carries
/// the Qt::ItemIsEditable flag (QTreeWidget item flags are per-item, not per-column).
class IntCellDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    /// @param editableColumn the single column whose cells may be edited as an integer.
    explicit IntCellDelegate(int editableColumn, QObject* parent = nullptr);

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override;

private:
    int _editableColumn;
};

} // namespace geck
