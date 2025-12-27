#include "MessageSelectorDialog.h"
#include "../../format/msg/Msg.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"

#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <spdlog/spdlog.h>

namespace geck {

MessageSelectorDialog::MessageSelectorDialog(const Msg* msgFile, int currentMessageId, QWidget* parent)
    : QDialog(parent)
    , _msgFile(msgFile)
    , _currentMessageId(currentMessageId)
    , _selectedMessageId(-1)
    , _mainLayout(nullptr)
    , _titleLabel(nullptr)
    , _messageList(nullptr)
    , _buttonLayout(nullptr)
    , _okButton(nullptr)
    , _cancelButton(nullptr) {

    setWindowTitle("Select Message");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    resize(ui::constants::dialog_sizes::MEDIUM_WIDTH, ui::constants::dialog_sizes::MEDIUM_HEIGHT);

    setupUI();
    populateMessages();
}

int MessageSelectorDialog::getSelectedMessageId() const {
    return _selectedMessageId;
}

void MessageSelectorDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(ui::constants::PANEL_MARGIN, ui::constants::PANEL_MARGIN, ui::constants::PANEL_MARGIN, ui::constants::PANEL_MARGIN);
    _mainLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // Title label
    _titleLabel = new QLabel("Select a message from the list:");
    _titleLabel->setStyleSheet(ui::theme::styles::boldLabelWithMargin());
    _mainLayout->addWidget(_titleLabel);

    // Message list
    _messageList = new QListWidget(this);
    _messageList->setAlternatingRowColors(true);
    _messageList->setSelectionMode(QAbstractItemView::SingleSelection);
    _messageList->setSortingEnabled(true);
    _mainLayout->addWidget(_messageList);

    connect(_messageList, &QListWidget::itemSelectionChanged, this, &MessageSelectorDialog::onSelectionChanged);
    connect(_messageList, &QListWidget::itemDoubleClicked, this, &MessageSelectorDialog::onItemDoubleClicked);

    // Button layout
    _buttonLayout = new QHBoxLayout();
    _buttonLayout->addStretch();

    _okButton = new QPushButton("OK", this);
    _okButton->setEnabled(false); // Disabled until selection is made
    _okButton->setDefault(true);
    connect(_okButton, &QPushButton::clicked, this, &QDialog::accept);
    _buttonLayout->addWidget(_okButton);

    _cancelButton = new QPushButton("Cancel", this);
    connect(_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    _buttonLayout->addWidget(_cancelButton);

    _mainLayout->addLayout(_buttonLayout);
}

void MessageSelectorDialog::populateMessages() {
    if (!_msgFile) {
        spdlog::error("MessageSelectorDialog: MSG file is null");
        return;
    }

    try {
        const auto& messages = _msgFile->getMessages();

        QListWidgetItem* currentItem = nullptr;

        for (const auto& [id, message] : messages) {
            // Create display text: "ID: 123 - Message text..."
            QString displayText = QString("ID: %1 - %2")
                                      .arg(id)
                                      .arg(QString::fromStdString(message.text));

            // Truncate very long messages for display
            if (displayText.length() > 120) {
                displayText = displayText.left(117) + "...";
            }

            QListWidgetItem* item = new QListWidgetItem(displayText);
            item->setData(Qt::UserRole, id);                        // Store the message ID
            item->setToolTip(QString::fromStdString(message.text)); // Full text as tooltip

            _messageList->addItem(item);

            // Remember the current message item for selection
            if (id == _currentMessageId) {
                currentItem = item;
            }
        }

        // Select and scroll to current message if found
        if (currentItem) {
            _messageList->setCurrentItem(currentItem);
            _messageList->scrollToItem(currentItem);
            _selectedMessageId = _currentMessageId;
            _okButton->setEnabled(true);
        }

        spdlog::debug("MessageSelectorDialog: Populated {} messages, current ID: {}", messages.size(), _currentMessageId);

    } catch (const std::exception& e) {
        spdlog::error("MessageSelectorDialog: Error populating messages: {}", e.what());

        // Add error item
        QListWidgetItem* errorItem = new QListWidgetItem("Error: Could not load messages");
        errorItem->setData(Qt::UserRole, -1);
        _messageList->addItem(errorItem);
    }
}

void MessageSelectorDialog::onSelectionChanged() {
    QListWidgetItem* currentItem = _messageList->currentItem();
    if (currentItem) {
        _selectedMessageId = currentItem->data(Qt::UserRole).toInt();
        _okButton->setEnabled(_selectedMessageId >= 0);
        spdlog::debug("MessageSelectorDialog: Selected message ID: {}", _selectedMessageId);
    } else {
        _selectedMessageId = -1;
        _okButton->setEnabled(false);
    }
}

void MessageSelectorDialog::onItemDoubleClicked(QListWidgetItem* item) {
    if (item && item->data(Qt::UserRole).toInt() >= 0) {
        _selectedMessageId = item->data(Qt::UserRole).toInt();
        accept(); // Close dialog with OK result
    }
}

} // namespace geck