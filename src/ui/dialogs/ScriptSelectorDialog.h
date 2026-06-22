#pragma once

#include "ui/common/BaseDialog.h"

#include <string>
#include <vector>

class QTableWidget;
class QLineEdit;

namespace geck {

namespace resource {
    class GameResources;
}

/// @brief Picks a script program from scripts.lst.
///
/// The returned value is the 0-based line index in scripts.lst, which is the
/// script "program index" the engine stores as Script::index (our
/// MapScript.script_id). The script *type* (item/critter) is decided by the
/// object being scripted, not here - see objectSetScript in the engine.
class ScriptSelectorDialog : public BaseDialog {
    Q_OBJECT

public:
    /// One selectable script: its program index (0-based scripts.lst line), the scripts.lst filename,
    /// and the scrname.msg description (may be empty).
    struct Entry {
        int index = 0;
        std::string filename;
        std::string name;
    };

    /// @param scripts      the rows to show (one per scripts.lst entry)
    /// @param currentIndex program index to preselect, or -1
    ScriptSelectorDialog(const std::vector<Entry>& scripts, int currentIndex, QWidget* parent = nullptr);

    /// Build a row for every scripts.lst entry, resolving each name via scrname.msg. Shared by the
    /// object-script and spatial-script pickers so they show the same data.
    static std::vector<Entry> buildEntries(resource::GameResources& resources);

    /// Selected program index, or -1 if none.
    int selectedIndex() const;

private slots:
    void onFilterChanged(const QString& text);

private:
    QLineEdit* _filterEdit;
    QTableWidget* _table;
};

} // namespace geck
