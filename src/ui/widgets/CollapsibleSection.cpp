#include "CollapsibleSection.h"

#include "ui/theme/ThemeManager.h"

#include <QLayout>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace geck {

CollapsibleSection::CollapsibleSection(const QString& key, const QString& title, QWidget* parent)
    : QWidget(parent)
    , _key(key)
    , _header(new QToolButton(this))
    , _body(new QWidget(this)) {
    _header->setText(title);
    _header->setCheckable(true);
    _header->setChecked(true);
    _header->setArrowType(Qt::DownArrow);
    _header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _header->setAutoRaise(true);
    _header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QFont font = _header->font();
    font.setBold(true);
    _header->setFont(font);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ui::theme::spacing::TIGHT);
    layout->addWidget(_header);
    layout->addWidget(_body);

    connect(_header, &QToolButton::toggled, this, [this](bool expanded) {
        _header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        _body->setVisible(expanded);
        Q_EMIT expandedChanged(expanded);
    });
}

void CollapsibleSection::setTitle(const QString& title) {
    _header->setText(title);
}

void CollapsibleSection::setContentLayout(QLayout* layout) {
    delete _body->layout();
    layout->setContentsMargins(ui::theme::spacing::LOOSE, 0, 0, 0);
    _body->setLayout(layout);
}

bool CollapsibleSection::isExpanded() const {
    return _header->isChecked();
}

void CollapsibleSection::setExpanded(bool expanded) {
    const QSignalBlocker blocker(_header);
    _header->setChecked(expanded);
    _header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    _body->setVisible(expanded);
}

} // namespace geck
