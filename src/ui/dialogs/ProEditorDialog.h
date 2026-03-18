#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <memory>

#include "../../format/frm/Frm.h"
#include "../../format/pro/Pro.h"
#include "../../format/pro/ProDataModels.h"
#include "../widgets/ProInfoPanelWidget.h"
#include "../widgets/ProPreviewPanelWidget.h"
#include "../widgets/ProCommonFieldsWidget.h"

namespace geck {

namespace resource {
    class GameResources;
}

class ProTabWidget;

/**
 * @brief PRO file editor dialog matching f2wedit functionality
 *
 * Provides comprehensive editing capabilities for PRO files with tabbed interface
 * supporting all item types: Armor, Container, Drug, Weapon, Ammo, Misc, Key
 */
class ProEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProEditorDialog(resource::GameResources& resources, std::shared_ptr<Pro> pro, QWidget* parent = nullptr);
    ~ProEditorDialog() = default;

    std::shared_ptr<Pro> getModifiedPro() const { return _pro; }

    static constexpr int MAX_LIGHT_RADIUS = 8;        // Wiki: Light radius "0..8 (hexes)"
    static constexpr int MAX_LIGHT_INTENSITY = 65536; // Wiki: Light intensity "0..65536"

private slots:
    void onAccept();
    void onFieldChanged();
    void onEditMessageClicked();

private:
    void setupUI();
    void setupTabs();
    void setupCommonTab();
    void setupTypeSpecificTabs();
    void setupMiscTab();
    ProTabWidget* createTypeSpecificWidget();
    void registerTypeSpecificWidget(ProTabWidget* widget);

    void loadProData();

    void saveProData();

    void refreshInfoPanel();
    void openFrmSelectorForLabel(QLabel* targetLabel, int32_t* fidStorage, Frm::FRM_TYPE objectType);

    QString getFrmFilename(int32_t fid);

    // UI Components
    QVBoxLayout* _mainLayout;
    QHBoxLayout* _contentLayout;
    QTabWidget* _tabWidget;
    QDialogButtonBox* _buttonBox;

    ProInfoPanelWidget* _infoPanelWidget;
    ProPreviewPanelWidget* _previewPanelWidget;

    // Tabs
    QWidget* _commonTab;
    QWidget* _miscTab;

    ProCommonFieldsWidget* _commonFieldsWidget;
    ProTabWidget* _typeSpecificWidget;

    std::shared_ptr<Pro> _pro;
    resource::GameResources& _resources;
};

} // namespace geck
