#pragma once

#include "ui/common/BaseDialog.h"

#include <vector>
#include <string>

class QListWidget;
class QLineEdit;

namespace geck {

/// @brief Picks a script program from scripts.lst.
///
/// The returned value is the 0-based line index in scripts.lst, which is the
/// script "program index" the engine stores as Script::index (our
/// MapScript.script_id). The script *type* (item/critter) is decided by the
/// object being scripted, not here - see objectSetScript in the engine.
class ScriptSelectorDialog : public BaseDialog {
    Q_OBJECT

public:
    /// @param scriptNames  scripts.lst entries (index == program index)
    /// @param currentIndex program index to preselect, or -1
    ScriptSelectorDialog(const std::vector<std::string>& scriptNames, int currentIndex, QWidget* parent = nullptr);

    /// Selected program index, or -1 if none.
    int selectedIndex() const;

private slots:
    void onFilterChanged(const QString& text);

private:
    QLineEdit* _filterEdit;
    QListWidget* _listWidget;
};

} // namespace geck
