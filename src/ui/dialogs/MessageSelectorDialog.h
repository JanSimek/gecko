#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <map>

namespace geck {

class Msg;

/**
 * @brief Dialog for selecting messages from MSG files
 * 
 * Displays all available messages from a MSG file and allows
 * the user to select one. Shows message ID and text content.
 */
class MessageSelectorDialog : public QDialog {
    Q_OBJECT

public:
    explicit MessageSelectorDialog(const Msg* msgFile, int currentMessageId, QWidget* parent = nullptr);
    ~MessageSelectorDialog() = default;

    // Returns the selected message ID, or -1 if cancelled
    int getSelectedMessageId() const;

private slots:
    void onSelectionChanged();
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();
    void populateMessages();
    
    const Msg* _msgFile;
    int _currentMessageId;
    int _selectedMessageId;
    
    // UI Components
    QVBoxLayout* _mainLayout;
    QLabel* _titleLabel;
    QListWidget* _messageList;
    QHBoxLayout* _buttonLayout;
    QPushButton* _okButton;
    QPushButton* _cancelButton;
};

} // namespace geck