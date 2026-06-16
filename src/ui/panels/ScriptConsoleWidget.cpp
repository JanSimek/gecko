#include "ScriptConsoleWidget.h"

#include "ui/IconHelper.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTextCursor>
#include <QVBoxLayout>

namespace geck {

ScriptConsoleWidget::ScriptConsoleWidget(QWidget* parent)
    : QWidget(parent)
    , _input(new QPlainTextEdit(this))
    , _output(new QPlainTextEdit(this))
    , _runButton(new QPushButton(tr("Run"), this)) {

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    _input->setFont(mono);
    _input->setPlaceholderText(tr("-- Luau generation script; the map API is the global 'api'.\n"
                                  "-- e.g.  for i = 0, 99 do api:paintFloor(i, 271) end"));

    _output->setFont(mono);
    _output->setReadOnly(true);
    _output->setPlaceholderText(tr("Script output and errors appear here."));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(_input, 3);

    _runButton->setIcon(createIcon(":/icons/actions/play.svg"));

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(_runButton);
    layout->addLayout(buttons);

    layout->addWidget(_output, 2);

    connect(_runButton, &QPushButton::clicked, this, &ScriptConsoleWidget::onRun);

    // Ctrl+Return runs the script straight from the editor. The shortcut is owned by _input (its
    // parent); we only connect to it, so a pointer-to-const suffices.
    const auto* runShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), _input);
    connect(runShortcut, &QShortcut::activated, this, &ScriptConsoleWidget::onRun);
}

void ScriptConsoleWidget::onRun() {
    const QString source = _input->toPlainText();
    if (source.trimmed().isEmpty()) {
        return;
    }
    Q_EMIT runRequested(source);
}

void ScriptConsoleWidget::setSource(const QString& source) {
    _input->setPlainText(source);
    _input->setFocus();
    _input->moveCursor(QTextCursor::End);
}

void ScriptConsoleWidget::showResult(bool ok, const QString& output, const QString& error) {
    QString text = output;
    if (!ok) {
        if (!text.isEmpty() && !text.endsWith('\n')) {
            text += '\n';
        }
        text += error;
    }
    if (text.isEmpty()) {
        text = ok ? tr("(script finished, no output)") : tr("(failed, no message)");
    }
    _output->setPlainText(text);
}

} // namespace geck
