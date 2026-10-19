#pragma once

#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QAction>
#include <QLabel>
#include <QObject>
#include <QString>
#include <QTableWidget>
#include <QTreeWidget>
#include <QWidget>

#include <functional>

// Widget lookups for the Qt tests. Each returns nullptr (or -1) when nothing matches, so a test
// REQUIREs what it needs rather than crashing on a missing widget.
namespace geck::test {

/// The first descendant of `root` of type T for which `matches` holds, or nullptr.
template <typename T>
T* findChildWhere(const QObject& root, const std::function<bool(const T&)>& matches) {
    for (T* child : root.findChildren<T*>()) {
        if (matches(*child)) {
            return child;
        }
    }
    return nullptr;
}

/// The button under `root` labelled exactly `text`.
inline QAbstractButton* findButton(const QWidget& root, const QString& text) {
    return findChildWhere<QAbstractButton>(root, [&text](const QAbstractButton& button) { return button.text() == text; });
}

/// `action`'s text without its mnemonic `&`s, as the user reads it.
inline QString plainText(const QAction& action) {
    QString text = action.text();
    text.remove('&');
    return text;
}

/// The action under `root` whose text, mnemonic `&` removed, is `text`.
inline QAction* findActionByText(const QObject& root, const QString& text) {
    return findChildWhere<QAction>(root, [&text](const QAction& action) { return plainText(action) == text; });
}

/// The widget of type T under `root` whose tooltip is `toolTip`.
template <typename T>
T* findByToolTip(const QWidget& root, const QString& toolTip) {
    return findChildWhere<T>(root, [&toolTip](const T& widget) { return widget.toolTip() == toolTip; });
}

/// The first label under `root` whose text contains `text`.
inline QLabel* labelContaining(const QWidget& root, const QString& text) {
    return findChildWhere<QLabel>(root, [&text](const QLabel& label) { return label.text().contains(text); });
}

/// The cell at (row, column), failing the test rather than crashing the suite when it is missing.
inline QTableWidgetItem& requireItem(const QTableWidget& table, int row, int column) {
    QTableWidgetItem* item = table.item(row, column);
    REQUIRE(item != nullptr);
    return *item;
}

/// The first row whose cell in `column` satisfies `matches`, or -1. Every row must have that cell.
inline int findTableRow(const QTableWidget& table, int column, const std::function<bool(const QTableWidgetItem&)>& matches) {
    for (int row = 0; row < table.rowCount(); ++row) {
        if (matches(requireItem(table, row, column))) {
            return row;
        }
    }
    return -1;
}

/// The item at any depth of `tree` whose first column reads exactly `text`.
inline QTreeWidgetItem* findTreeItem(const QTreeWidget& tree, const QString& text) {
    return tree.findItems(text, Qt::MatchExactly | Qt::MatchRecursive).value(0);
}

/// The number of items without children, at any depth of `tree`, that are not hidden themselves.
inline int visibleLeafCount(const QTreeWidget& tree) {
    int count = 0;
    std::function<void(const QTreeWidgetItem&)> visit = [&count, &visit](const QTreeWidgetItem& item) {
        if (item.childCount() == 0) {
            count += item.isHidden() ? 0 : 1;
        }
        for (int index = 0; index < item.childCount(); ++index) {
            visit(*item.child(index));
        }
    };
    for (int index = 0; index < tree.topLevelItemCount(); ++index) {
        visit(*tree.topLevelItem(index));
    }
    return count;
}

} // namespace geck::test
