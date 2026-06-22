#pragma once

#include "ui/UIConstants.h"

#include <QPushButton>
#include <QSize>

namespace geck::ui {

/// Apply the standard sizing for an icon action button: a consistent icon size and a minimum height so
/// the button does not shrink and clip its icon when the dialog is resized. Centralizes the sizing that
/// was previously set ad-hoc (or omitted) per button, so action buttons look consistent across dialogs.
inline void styleActionButton(QPushButton* button) {
    if (button == nullptr) {
        return;
    }
    button->setIconSize(QSize(constants::sizes::ICON_SIZE_SMALL, constants::sizes::ICON_SIZE_SMALL));
    button->setMinimumHeight(constants::sizes::ACTION_BUTTON_HEIGHT);
}

} // namespace geck::ui
