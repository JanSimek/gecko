#pragma once

#include <QLabel>

namespace geck {

/// Read-only text that shortens to the width it is given, with the full value in its tooltip and on
/// Copy. Unlike a read-only line edit it asks for almost no minimum width, so a long value never
/// widens the panel it sits in.
class ElidedLabel : public QLabel {
    Q_OBJECT

public:
    explicit ElidedLabel(Qt::TextElideMode mode = Qt::ElideRight, QWidget* parent = nullptr);

    void setFullText(const QString& text);
    const QString& fullText() const { return _fullText; }

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void updateElidedText();

    QString _fullText;
    Qt::TextElideMode _mode;
};

} // namespace geck
