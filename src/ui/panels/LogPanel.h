#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTreeView;

namespace geck {

class LogModel;
class LogFilterProxy;

/// Dockable log & diagnostics panel: a filterable view over the LogModel that the in-app spdlog
/// sink feeds, so load-time warnings (missing tile art, unresolved sprites, ...) are visible in
/// the UI instead of only on the console. Offers a minimum-level filter, a message text filter,
/// copy (selection or the whole filtered view), and clear.
class LogPanel : public QWidget {
    Q_OBJECT

public:
    explicit LogPanel(QWidget* parent = nullptr);

    /// Attach the record store. The panel does not own the model.
    void setModel(LogModel* model);

private:
    void onLevelFilterChanged(int index);
    void onSearchTextChanged(const QString& text);
    void onCopy();
    void onClear();

    LogModel* _model = nullptr;
    LogFilterProxy* _proxy;
    QTreeView* _view;
    QComboBox* _levelFilter;
    QLineEdit* _search;
    QPushButton* _copyButton;
    QPushButton* _clearButton;
    bool _followTail = true;
};

} // namespace geck
