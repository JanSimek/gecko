#pragma once

#include <QWidget>

class QPlainTextEdit;
class QPushButton;

namespace geck {

/// A minimal Lua/Luau script console: an input editor, a Run button (also Ctrl+Return), and a
/// read-only output pane. It is scripting-agnostic — the host connects runRequested() to the
/// script runtime and feeds the outcome back via showResult().
class ScriptConsoleWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScriptConsoleWidget(QWidget* parent = nullptr);

    /// Display a run's outcome: the script's print() output, plus an error line when it failed.
    void showResult(bool ok, const QString& output, const QString& error);

    /// Replace the input editor's contents (e.g. load a script the user picked elsewhere) and
    /// focus it, ready to Run.
    void setSource(const QString& source);

Q_SIGNALS:
    /// Emitted when the user runs the (non-empty) script in the editor.
    void runRequested(const QString& source);

private:
    void onRun();

    QPlainTextEdit* _input;
    QPlainTextEdit* _output;
    QPushButton* _runButton;
};

} // namespace geck
