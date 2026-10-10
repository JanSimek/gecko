#pragma once

#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>

#include "ui/ScriptSourceService.h"

class QCompleter;
class QLabel;
class QLineEdit;
class QStandardItemModel;
class QToolButton;
class QTreeWidget;

namespace geck {

class ElidedLabel;
class Map;
struct MapObject;
struct MapScript;
namespace resource {
    class GameResources;
    struct ScriptSourceFacts;
}

/// The Selection panel's script section for one object: an inline searchable picker (choosing a
/// script attaches it), the attached script's identity in every index base, what its source does that
/// overrides this object's map data, and its local variables. Attaching, detaching and opening the
/// source are requested through signals so they stay undoable / owned by MainWindow.
class ObjectScriptSection : public QWidget {
    Q_OBJECT

public:
    using StatusProvider = std::function<ScriptSourceService::ScriptStatus(int programIndex)>;

    explicit ObjectScriptSection(resource::GameResources& resources, QWidget* parent = nullptr);

    /// How to learn whether a script is compiled and what its source holds (MainWindow passes
    /// ScriptSourceService::scriptStatus). Without one, only the scripts.lst identity is shown.
    void setStatusProvider(StatusProvider provider);

    /// Show the script attached to `object` in `map`, or the empty picker when none is.
    void showObject(Map* map, std::shared_ptr<MapObject> object);

signals:
    void attachRequested(int programIndex);
    void detachRequested();
    void editSourceRequested(int programIndex);
    /// Raise the Scripts panel on the row of the script with this SID.
    void showInScriptsPanelRequested(uint32_t sid);

private:
    void ensureCatalog();
    void choose(int programIndex);
    void showPickerText();
    void browse();
    void refreshDetails(const MapScript* script, const ScriptSourceService::ScriptStatus& status);
    void refreshLocalVariables(const MapScript* script, const resource::ScriptSourceFacts& facts);
    const MapScript* attachedScript() const;
    bool isCritter() const;

    resource::GameResources& _resources;
    StatusProvider _statusProvider;
    Map* _map = nullptr;
    std::shared_ptr<MapObject> _object;
    int _programIndex = -1; // the attached script's 0-based scripts.lst index, or -1

    QLineEdit* _picker;
    QCompleter* _completer;
    QStandardItemModel* _catalog; // every scripts.lst entry, built on first use
    QString _shownText;           // the picker text for the attached script, restored after a search
    QToolButton* _browseButton;
    QToolButton* _moreButton;
    QAction* _openSourceAction;
    QAction* _showInPanelAction;
    QAction* _detachAction;
    ElidedLabel* _identity;
    QLabel* _warnings; // what stops the script working, or what it overrides
    QLabel* _hint;     // how to get more detail (no source found)
    QLabel* _localVarsTitle;
    QTreeWidget* _localVars;
};

} // namespace geck
