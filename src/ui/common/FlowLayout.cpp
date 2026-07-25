#include "FlowLayout.h"

#include <QWidget>

namespace geck::ui {

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent)
    , _hSpace(hSpacing)
    , _vSpace(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int hSpacing, int vSpacing)
    : _hSpace(hSpacing)
    , _vSpace(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout() {
    while (QLayoutItem* item = takeAt(0)) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem* item) {
    _items.push_back(item);
}

int FlowLayout::horizontalSpacing() const {
    return _hSpace >= 0 ? _hSpace : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const {
    return _vSpace >= 0 ? _vSpace : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::count() const {
    return static_cast<int>(_items.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const {
    return (index >= 0 && index < count()) ? _items[static_cast<std::size_t>(index)] : nullptr;
}

QLayoutItem* FlowLayout::takeAt(int index) {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    QLayoutItem* item = _items[static_cast<std::size_t>(index)];
    _items.erase(_items.begin() + index);
    return item;
}

Qt::Orientations FlowLayout::expandingDirections() const {
    return {};
}

bool FlowLayout::hasHeightForWidth() const {
    return true;
}

int FlowLayout::heightForWidth(int width) const {
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const {
    return minimumSize();
}

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (const QLayoutItem* item : _items) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    getContentsMargins(&left, &top, &right, &bottom);
    const QRect effectiveRect = rect.adjusted(left, top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;

    for (QLayoutItem* item : _items) {
        const QWidget* widget = item->widget();
        int spaceX = horizontalSpacing();
        if (spaceX == -1) {
            spaceX = widget->style()->layoutSpacing(
                QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
        }
        int spaceY = verticalSpacing();
        if (spaceY == -1) {
            spaceY = widget->style()->layoutSpacing(
                QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);
        }

        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }

        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        }

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + bottom;
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const {
    QObject* parentObject = parent();
    if (parentObject == nullptr) {
        return -1;
    }
    if (parentObject->isWidgetType()) {
        auto* parentWidget = static_cast<QWidget*>(parentObject);
        return parentWidget->style()->pixelMetric(pm, nullptr, parentWidget);
    }
    return static_cast<QLayout*>(parentObject)->spacing();
}

} // namespace geck::ui
