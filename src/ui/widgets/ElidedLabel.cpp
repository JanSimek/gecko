#include "ElidedLabel.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>

namespace geck {

namespace {
    constexpr int MIN_VISIBLE_WIDTH = 40; // enough for a few characters and the ellipsis
}

ElidedLabel::ElidedLabel(Qt::TextElideMode mode, QWidget* parent)
    : QLabel(parent)
    , _mode(mode) {
    setTextInteractionFlags(Qt::TextSelectableByMouse);
    setTextFormat(Qt::PlainText);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void ElidedLabel::setFullText(const QString& text) {
    _fullText = text;
    setToolTip(text);
    updateElidedText();
    updateGeometry();
}

QSize ElidedLabel::minimumSizeHint() const {
    return { MIN_VISIBLE_WIDTH, QLabel::minimumSizeHint().height() };
}

QSize ElidedLabel::sizeHint() const {
    return { fontMetrics().horizontalAdvance(_fullText) + 2 * margin(), QLabel::sizeHint().height() };
}

void ElidedLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    updateElidedText();
}

void ElidedLabel::contextMenuEvent(QContextMenuEvent* event) {
    // The shown text may be shortened; Copy always takes the whole value.
    QMenu menu(this);
    menu.addAction("Copy", this, [this]() { QApplication::clipboard()->setText(_fullText); });
    menu.exec(event->globalPos());
}

void ElidedLabel::updateElidedText() {
    QLabel::setText(fontMetrics().elidedText(_fullText, _mode, contentsRect().width()));
}

} // namespace geck
