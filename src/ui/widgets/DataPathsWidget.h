#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
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

private:
    void setupUI();
    void setupConnections();
    // Insert a path as a table row. atTop=true makes it the highest priority (the first row);
    // otherwise it is appended as the lowest. Returns false if the path is already present.
    bool addPathRow(const std::filesystem::path& path, bool atTop);
    void removeSelectedPath();
    void updateButtonStates();
    void moveSelectedPath(int offset);
    void renumberPriorities();
    bool isProtectedRow(int row) const;
    int selectedRow() const;

    // Highest priority is the top row; the stored order is lowest-priority-first, so the table
    // displays it reversed (see getDataPaths/setDataPaths). Columns:
    enum Column { PriorityColumn = 0,
        PathColumn = 1,
        ColumnCount = 2 };

    // UI Components
    QVBoxLayout* _layout;
    QLabel* _helpLabel;
    QTableWidget* _pathsTable;
    QHBoxLayout* _controlLayout;
    QPushButton* _addButton;
    QPushButton* _removeButton;
    QPushButton* _moveUpButton;
    QPushButton* _moveDownButton;
    QPushButton* _autoDetectButton;
    QProgressBar* _progressBar;

    std::shared_ptr<Settings> _settings;
};

} // namespace geck
