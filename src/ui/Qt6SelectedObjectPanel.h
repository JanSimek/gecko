#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QGroupBox>
#include <QScrollArea>
#include <memory>

#include "../editor/Object.h"

namespace geck {

class Qt6SelectedObjectPanel : public QWidget {
    Q_OBJECT

public:
    explicit Qt6SelectedObjectPanel(QWidget* parent = nullptr);

public slots:
    void selectObject(std::shared_ptr<Object> selectedObject);

private:
    void setupUI();
    void updateObjectInfo();
    void clearObjectInfo();

    QVBoxLayout* _mainLayout;
    QScrollArea* _scrollArea;
    QWidget* _contentWidget;
    QVBoxLayout* _contentLayout;
    
    // Object info widgets
    QGroupBox* _objectInfoGroup;
    QLabel* _spriteLabel;
    QLineEdit* _nameEdit;
    QLineEdit* _typeEdit;
    QSpinBox* _messageIdSpin;
    QSpinBox* _positionSpin;
    QSpinBox* _protoPidSpin;
    
    std::optional<std::shared_ptr<Object>> _selectedObject;
};

} // namespace geck