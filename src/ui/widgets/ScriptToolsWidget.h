#pragma once

#include <QGroupBox>

class QFormLayout;
class QLineEdit;
class QPushButton;

namespace geck {

/// @brief Preferences group for the external SSL script toolchain.
///
/// Two browse-able executable paths: the sslc compiler (.ssl -> .int) and the int2ssl
/// decompiler (.int -> .ssl). The binaries are user-provided — licensing keeps them out of the
/// Gecko distribution (sslc ships no license file; int2ssl is GPL-3.0) — so this page is where
/// the user points Gecko at their copies.
class ScriptToolsWidget : public QGroupBox {
    Q_OBJECT

public:
    explicit ScriptToolsWidget(QWidget* parent = nullptr);

    QString getCompilerPath() const;
    void setCompilerPath(const QString& path);

    QString getDecompilerPath() const;
    void setDecompilerPath(const QString& path);

signals:
    void configurationChanged();

private:
    // One "<label>: [path edit] [Browse...]" form row; returns the created path edit.
    QLineEdit* addPathRow(QFormLayout* form, const QString& label, const QString& placeholder,
        const QString& dialogTitle);

    QLineEdit* _compilerPathEdit = nullptr;
    QLineEdit* _decompilerPathEdit = nullptr;
};

} // namespace geck
