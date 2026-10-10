#pragma once

#include <QWidget>

class QLayout;
class QToolButton;

namespace geck {

/// A titled section whose body folds away under a disclosure arrow, for long single-column panels.
/// `key` names the section for remembering its state across runs (see Settings::getCollapsedSections).
class CollapsibleSection : public QWidget {
    Q_OBJECT

public:
    CollapsibleSection(const QString& key, const QString& title, QWidget* parent = nullptr);

    const QString& key() const { return _key; }
    void setTitle(const QString& title);
    /// The body's layout; the section takes ownership.
    void setContentLayout(QLayout* layout);

    bool isExpanded() const;
    /// Folds or unfolds without emitting expandedChanged (for restoring saved state).
    void setExpanded(bool expanded);

signals:
    /// The user folded or unfolded the section.
    void expandedChanged(bool expanded);

private:
    QString _key;
    QToolButton* _header;
    QWidget* _body;
};

} // namespace geck
