#include "ScriptToolsWidget.h"

#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>

namespace geck {

ScriptToolsWidget::ScriptToolsWidget(QWidget* parent)
    : QGroupBox("SSL Script Tools", parent) {

    auto* layout = new QVBoxLayout(this);

    auto* helpLabel = new QLabel(
        "External tools for the scripts behind scripts.lst entries: sslc (compile.exe) compiles "
        ".ssl source to the .int bytecode the engine loads; int2ssl recovers editable source from "
        "an .int when none is available. Both are separate downloads — point Gecko at your copies.");
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet(ui::theme::styles::helpText());
    layout->addWidget(helpLabel);

    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    const auto addPathRow = [this, form](const QString& label, const QString& placeholder,
                                const QString& dialogTitle) {
        auto* pathEdit = new QLineEdit(this);
        pathEdit->setPlaceholderText(placeholder);
        connect(pathEdit, &QLineEdit::textChanged, this, &ScriptToolsWidget::configurationChanged);

        auto* browseButton = new QPushButton("Browse...", this);
        browseButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
        connect(browseButton, &QPushButton::clicked, this, [this, pathEdit, dialogTitle]() {
            const QString currentPath = pathEdit->text();
            const QString startPath = currentPath.isEmpty()
                ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                : QFileInfo(currentPath).absolutePath();
            const QString chosen = QFileDialog::getOpenFileName(this, dialogTitle, startPath,
                "Executable Files (*.exe *);;All Files (*)");
            if (!chosen.isEmpty()) {
                pathEdit->setText(chosen);
            }
        });

        auto* row = new QHBoxLayout();
        row->addWidget(pathEdit, 1);
        row->addWidget(browseButton);
        form->addRow(label, row);
        return pathEdit;
    };

    _compilerPathEdit = addPathRow("sslc compiler:", "Path to compile / sslc executable...",
        "Select the sslc Compiler Executable");
    _decompilerPathEdit = addPathRow("int2ssl decompiler:", "Path to int2ssl executable...",
        "Select the int2ssl Decompiler Executable");

    layout->addLayout(form);
}

QString ScriptToolsWidget::getCompilerPath() const {
    return _compilerPathEdit->text();
}

void ScriptToolsWidget::setCompilerPath(const QString& path) {
    _compilerPathEdit->setText(path);
}

QString ScriptToolsWidget::getDecompilerPath() const {
    return _decompilerPathEdit->text();
}

void ScriptToolsWidget::setDecompilerPath(const QString& path) {
    _decompilerPathEdit->setText(path);
}

} // namespace geck
