#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QIcon>
#include <QProgressBar>
#include <filesystem>
#include <memory>
#include <vector>

namespace geck {

class Settings;

/**
 * @brief Widget for managing Fallout 2 data paths
 *
 * Provides UI for adding, removing, and auto-detecting Fallout 2 data paths.
 * Handles validation and visual feedback for path entries.
 */
class DataPathsWidget : public QGroupBox {
    Q_OBJECT

public:
    explicit DataPathsWidget(std::shared_ptr<Settings> settings, QWidget* parent = nullptr);
    ~DataPathsWidget() = default;

    // Data access
    std::vector<std::filesystem::path> getDataPaths() const;
    void setDataPaths(const std::vector<std::filesystem::path>& paths);

    // Explicit default save location (empty = unset -> the highest-priority folder is used).
    // Kept as a path, not a row, so reordering rows never moves the marker.
    std::filesystem::path getWritableDataPath() const;
    void setWritableDataPath(const std::filesystem::path& path);

    // Folders marked as SSL script-source trees (a subset of the data paths). Kept as paths, not
    // rows, so reordering never moves a marker.
    std::vector<std::filesystem::path> getScriptSourcePaths() const;
    void setScriptSourcePaths(const std::vector<std::filesystem::path>& paths);

    // Validation
    void validatePaths();

    // Status updates
    void setStatusMessage(const QString& message, const QString& styleClass = "normal");

signals:
    void dataPathsChanged();
    void statusChanged(const QString& message, const QString& styleClass);

private slots:
    void onAddFolder(); // pick a folder; its master.dat/critter.dat are added as explicit entries too
    void onAddDat();    // pick a single .dat file
    void onRemovePath();
    void onAutoDetect();
    void onSelectionChanged();
    void onCellDoubleClicked(int row, int column);
    void onToggleSaveLocation(); // mark the selected folder as the save location (or clear it)
    void onToggleScriptSource(); // mark the selected folder as an SSL script-source tree (or clear it)

private:
    void setupUI();
    void setupConnections();
    // Insert a path as a table row. atTop=true makes it the highest priority (the first row);
    // otherwise it is appended as the lowest. Returns false if the path is already present.
    bool addPathRow(const std::filesystem::path& path, bool atTop);
    // Add a folder AND its master.dat/critter.dat as explicit rows (util::expandDataPaths), keeping the
    // DATs in their legacy priority order regardless of atTop. Returns how many rows were added.
    int addFolderExpanded(const std::filesystem::path& folder, bool atTop);
    void removeSelectedPath();
    void updateButtonStates();
    void moveSelectedPath(int offset);
    void renumberPriorities();
    bool isProtectedRow(int row) const;
    int selectedRow() const;
    std::filesystem::path pathAtRow(int row) const; // normalized, empty if the row has no path item
    // A row can be marked (as the save location or a script source) only if it's a real folder
    // (not a .dat, not missing, not the protected built-in resources path).
    bool isMarkableRow(int row) const;
    // Whether `path` is the folder currently marked as the save location. Equivalence, not string
    // equality: the marker is stored as the user picked it, so a symlink or a redundant "/." would
    // otherwise read as a different folder here than it does in Settings and WritableDataRoot, and
    // the badge would call a marker unusable that those two still honour.
    bool isMarkedSaveLocation(const std::filesystem::path& path) const;
    // Re-derive each row's save-location badge (bold + save icon for the explicit marker, italic for
    // the positional default) from the current rows + marker. Called whenever either changes.
    void refreshSaveLocationMarkers();
    // Drop any script-source marker whose folder is no longer among the rows.
    void pruneScriptSourceMarkers();
    // The file-type icon (from the shared icon pack) for a row: warning / .dat / script-source
    // folder / plain folder. The save-location role is shown by font weight, not the icon.
    QIcon iconForRow(const std::filesystem::path& path, bool isScriptSource) const;

    // Highest priority is the top row; the stored order is lowest-priority-first, so the table
    // displays it reversed (see getDataPaths/setDataPaths). Columns:
    enum Column { PriorityColumn = 0,
        PathColumn = 1,
        ColumnCount = 2 };

    // UI Components
    QVBoxLayout* _layout = nullptr;
    QLabel* _helpLabel = nullptr;
    QTableWidget* _pathsTable = nullptr;
    QLayout* _controlLayout = nullptr; // the action column beside the list
    QLabel* _statusLabel = nullptr;    // this section's own status line
    QPushButton* _addButton = nullptr;
    QPushButton* _removeButton = nullptr;
    QPushButton* _moveUpButton = nullptr;
    QPushButton* _moveDownButton = nullptr;
    QPushButton* _saveLocationButton = nullptr;
    QPushButton* _scriptSourceButton = nullptr;
    QPushButton* _autoDetectButton = nullptr;
    QProgressBar* _progressBar = nullptr;

    std::shared_ptr<Settings> _settings;
    std::filesystem::path _writableDataPath;               // local copy; persisted by SettingsDialog on save
    std::vector<std::filesystem::path> _scriptSourcePaths; // local copy; persisted by SettingsDialog on save
};

} // namespace geck
