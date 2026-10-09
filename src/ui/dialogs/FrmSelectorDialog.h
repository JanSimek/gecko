#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTreeWidget>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QSplitter>

#include <optional>

#include "format/frm/Frm.h"
#include "format/frm/FrmId.h"
#include "format/pro/Pro.h"

namespace geck {

namespace resource {
    class GameResources;
}

class FrmSelectorDialog : public QDialog {
    Q_OBJECT

public:
    explicit FrmSelectorDialog(resource::GameResources& resources, QWidget* parent = nullptr);

    static std::optional<ObjectType> filterForObjectType(ObjectType objectType);
    static std::optional<ObjectType> filterForFid(uint32_t fid);

    /// Animation-type code that marks a critter FID made up for art absent from critters.lst. No
    /// engine animation uses it, so such a FID is display-only and never written to a map.
    static constexpr uint32_t CUSTOM_FID_ANIMATION_TYPE = 0xFF;
    static bool isCustomFid(uint32_t fid) { return FrmId(fid).animationType() == CUSTOM_FID_ANIMATION_TYPE; }

    /**
     * @brief Get the selected FRM PID
     * @return The FRM PID (FID) selected by the user, or std::nullopt if no
     *         valid FID could be determined for the current selection
     */
    std::optional<uint32_t> getSelectedFrmPid() const { return _selectedFrmPid; }

    /**
     * @brief Get the selected FRM path
     * @return The FRM path selected by the user, or empty string if none selected
     */
    std::string getSelectedFrmPath() const { return _frmPathEdit->text().toStdString(); }

    /**
     * @brief Set the initial FRM PID to display
     * @param frmPid The FRM PID to initially select
     */
    void setInitialFrmPid(uint32_t frmPid);

    /**
     * @brief Set object type filter for the FRM list
     * @param objectType Optional object type filter for the FRM list
     */
    void setObjectTypeFilter(std::optional<ObjectType> objectType);

private slots:
    void onSearchTextChanged();
    void onFrmListSelectionChanged();
    void onFrmPidChanged();
    void onAccepted();
    void onRejected();

private:
    void setupUI();
    void populateFrmList();
    void updatePreview();
    void filterFrmList(const QString& searchText);
    std::optional<uint32_t> deriveFrmPidFromPath(const std::string& frmPath);
    uint32_t tryFallbackFidDerivation(const std::string& normalizedPath,
        const std::string& filename,
        ObjectType frmType);

    // Animation grouping helpers
    std::string getGroupingKey(const std::string& frmPath);
    bool isCritterGroup(const std::string& groupName);
    std::string getAnimationSortKey(const std::string& frmPath);
    QString createDisplayName(const std::string& frmPath);

    // UI Components
    QVBoxLayout* _mainLayout;
    QSplitter* _splitter;

    // Left panel - FRM list
    QWidget* _listPanel;
    QVBoxLayout* _listLayout;
    QLineEdit* _searchEdit;
    QTreeWidget* _frmTreeWidget;

    // Right panel - Preview and details
    QWidget* _previewPanel;
    QVBoxLayout* _previewLayout;
    QGroupBox* _previewGroup;
    QLabel* _previewLabel;
    QGroupBox* _detailsGroup;
    QFormLayout* _detailsLayout;
    QSpinBox* _frmPidSpin;
    QLineEdit* _frmPathEdit;

    // Button box
    QHBoxLayout* _buttonLayout;
    QPushButton* _okButton;
    QPushButton* _cancelButton;

    // Data
    resource::GameResources& _resources;
    std::optional<uint32_t> _selectedFrmPid;
    std::vector<std::pair<uint32_t, std::string>> _frmFiles; // PID, Path pairs
    std::optional<ObjectType> _objectTypeFilter;
};

} // namespace geck
