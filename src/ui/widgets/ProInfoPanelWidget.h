#pragma once

#include <QWidget>

#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QVBoxLayout;
class QSpinBox;

namespace geck {

class Pro;

namespace resource {
    class GameResources;
}

class ProInfoPanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProInfoPanelWidget(QWidget* parent = nullptr);
    ~ProInfoPanelWidget() override = default;

    void setPreviewWidget(QWidget* previewWidget);
    void refreshFromPro(resource::GameResources& resources, const std::shared_ptr<Pro>& pro, uint32_t currentPid);

    void setNameAndDescription(const QString& name, const QString& description);
    void setNameText(const QString& name);
    QString nameText() const;
    void setDescriptionText(const QString& description);
    void setPid(int pid);
    int pid() const;
    void setFilenameText(const QString& filename);
    QString filenameText() const;
    QString windowTitleText() const;

signals:
    void editMessageRequested();
    void fieldChanged();

private:
    void setupUI();

    QVBoxLayout* _mainLayout = nullptr;
    QLabel* _nameLabel = nullptr;
    QPushButton* _editMessageButton = nullptr;
    QWidget* _previewContainer = nullptr;
    QVBoxLayout* _previewLayout = nullptr;
    QWidget* _previewWidget = nullptr;
    QTextEdit* _descriptionEdit = nullptr;
    QSpinBox* _pidEdit = nullptr;
    QLineEdit* _filenameEdit = nullptr;
    QString _windowTitleText;
};

} // namespace geck
