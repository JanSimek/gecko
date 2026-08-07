#pragma once

#include "ui/theme/ThemeManager.h"

#include <QLabel>
#include <QString>

namespace geck::ui {

/// Apply one of the shared status styles to a status label by name ("warning", "error", "success",
/// "info", anything else being the neutral style), and show the label only when it has something to
/// say. Sections that report their own status all speak the same vocabulary this way, rather than
/// each repeating the mapping from name to stylesheet.
inline void setStatusText(QLabel* label, const QString& message, const QString& styleClass) {
    if (label == nullptr) {
        return;
    }

    label->setText(message);
    label->setVisible(!message.isEmpty());

    if (styleClass == "warning") {
        label->setStyleSheet(theme::styles::statusWarning());
    } else if (styleClass == "error") {
        label->setStyleSheet(theme::styles::statusError());
    } else if (styleClass == "success") {
        label->setStyleSheet(theme::styles::statusSuccess());
    } else if (styleClass == "info") {
        label->setStyleSheet(theme::styles::statusInfo());
    } else {
        label->setStyleSheet(theme::styles::statusNormal());
    }
}

} // namespace geck::ui
