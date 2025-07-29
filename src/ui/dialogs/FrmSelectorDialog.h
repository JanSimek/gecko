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

namespace geck {

class FrmSelectorDialog : public QDialog {
    Q_OBJECT

public:
    explicit FrmSelectorDialog(QWidget* parent = nullptr);

    /**
     * @brief Get the selected FRM PID
     * @return The FRM PID (FID) selected by the user, or 0 if none selected
     */
    uint32_t getSelectedFrmPid() const { return _selectedFrmPid; }
    
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
     * @param objectType The object type to filter by (0=items, 1=critters, etc.)
     */
    void setObjectTypeFilter(uint32_t objectType);

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
    uint32_t deriveFrmPidFromPath(const std::string& frmPath);
    uint32_t tryFallbackFidDerivation(const std::string& normalizedPath, 
                                     const std::string& filename, 
                                     uint32_t frmType);
                                     
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
    uint32_t _selectedFrmPid;
    std::vector<std::pair<uint32_t, std::string>> _frmFiles; // PID, Path pairs
    uint32_t _objectTypeFilter; // Filter by object type (0=items, 1=critters, etc.)
};

} // namespace geck