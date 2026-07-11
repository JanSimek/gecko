#pragma once

#include <QDialog>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace geck::plugin {
class PluginManager;
}

namespace geck {

/// The editor's Plugin Manager: lists every discovered plugin (user + bundled), lets the user
/// enable/disable one, rescan the plugin directories, and read the selected plugin's console
/// (its print() output and any fault/traceback). A thin view over PluginManager — it holds no
/// plugin state of its own and re-reads PluginManager::list() after every action.
///
/// MVP scope note (shown in the dialog): enabled plugins are read-only and run their entry script
/// once; tools, panels, menus and map-write permissions arrive in later phases.
class PluginManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginManagerDialog(plugin::PluginManager& manager, QWidget* parent = nullptr);
    ~PluginManagerDialog() override = default;

private slots:
    void onSelectionChanged();
    void onEnableClicked();
    void onDisableClicked();
    void onRescanClicked();

private:
    void setupUI();
    // Rebuild the list from PluginManager::list(), keeping the current selection by id if possible.
    void refresh();
    // The id of the currently selected row, or an empty string when nothing is selected.
    QString selectedId() const;

    plugin::PluginManager& _manager;

    QListWidget* _list = nullptr;
    QLabel* _title = nullptr;
    QLabel* _meta = nullptr;
    QLabel* _description = nullptr;
    QLabel* _status = nullptr;
    QPlainTextEdit* _console = nullptr;
    QPushButton* _enableButton = nullptr;
    QPushButton* _disableButton = nullptr;
    QPushButton* _rescanButton = nullptr;
};

} // namespace geck
