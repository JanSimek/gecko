#pragma once

#include <QLayout>
#include <QRect>
#include <QStyle>

#include <vector>

namespace geck::ui {

/// A layout that arranges its items left-to-right and wraps to the next line when the available
/// width runs out (the canonical Qt "Flow Layout" example). Used for button rows that must stay
/// readable without pinning a wide minimum: on a small screen the buttons reflow onto extra lines
/// instead of forcing the whole dialog wider than the screen.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    [[nodiscard]] int horizontalSpacing() const;
    [[nodiscard]] int verticalSpacing() const;
    [[nodiscard]] Qt::Orientations expandingDirections() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] int count() const override;
    [[nodiscard]] QLayoutItem* itemAt(int index) const override;
    [[nodiscard]] QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    [[nodiscard]] QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    [[nodiscard]] int smartSpacing(QStyle::PixelMetric pm) const;

    std::vector<QLayoutItem*> _items;
    int _hSpace;
    int _vSpace;
};

} // namespace geck::ui
