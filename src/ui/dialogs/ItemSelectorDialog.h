#pragma once

#include <QDialog>

#include <cstdint>
#include <optional>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QSpinBox;
class QPushButton;

namespace geck {

namespace resource {
    class GameResources;
}

/// Browsable picker for an inventory item: lists every item proto (from proto/items/items.lst) by name,
/// type and PID with a search filter and a sprite preview, plus an amount spinner. Replaces the raw
/// "enter a PID in hex or decimal" prompt in the inventory editor. Returns the chosen item PID + amount.
class ItemSelectorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ItemSelectorDialog(resource::GameResources& resources, QWidget* parent = nullptr);

    /// The selected item PID, or nullopt if the dialog was cancelled / nothing was chosen.
    std::optional<uint32_t> selectedPid() const { return _selectedPid; }
    /// The chosen amount (>= 1). Only meaningful when selectedPid() has a value.
    int selectedAmount() const;

private slots:
    void onSearchTextChanged(const QString& text) const;
    void onSelectionChanged();
    void onItemActivated(const QTreeWidgetItem* item, int column);

private:
    void setupUI();
    void populate();
    void updatePreview(const QTreeWidgetItem* item);

    resource::GameResources& _resources;

    QLineEdit* _search = nullptr;
    QTreeWidget* _tree = nullptr;
    QLabel* _previewIcon = nullptr;
    QLabel* _previewName = nullptr;
    QLabel* _previewType = nullptr;
    QLabel* _previewPid = nullptr;
    QSpinBox* _amountSpin = nullptr;
    QPushButton* _okButton = nullptr;

    std::optional<uint32_t> _selectedPid;
};

} // namespace geck
