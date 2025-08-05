#include "AboutDialog.h"

#include <QApplication>
#include <QIcon>
#include <QFont>
#include <QPixmap>

#include "version.h"
#include "../../Application.h"

namespace geck {

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _contentLayout(nullptr)
    , _textLayout(nullptr)
    , _buttonLayout(nullptr)
    , _iconLabel(nullptr)
    , _titleLabel(nullptr)
    , _descriptionLabel(nullptr)
    , _okButton(nullptr)
{
    setupUI();
}

void AboutDialog::setupUI() {
    setWindowTitle("About");
    setModal(true);
    setFixedSize(400, 200);
    
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(20, 20, 20, 20);
    _mainLayout->setSpacing(15);
    
    createContent();
    createButtons();
}

void AboutDialog::createContent() {
    _contentLayout = new QHBoxLayout();
    _contentLayout->setSpacing(15);
    
    // Create icon label
    _iconLabel = new QLabel();
    _iconLabel->setFixedSize(64, 64);
    _iconLabel->setScaledContents(true);
    
    // Load application icon
    std::filesystem::path iconPath = Application::getResourcesPath() / "icon.png";
    QPixmap iconPixmap(QString::fromStdString(iconPath.string()));
    if (!iconPixmap.isNull()) {
        _iconLabel->setPixmap(iconPixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        // Fallback to application icon
        QIcon appIcon = QApplication::windowIcon();
        if (!appIcon.isNull()) {
            _iconLabel->setPixmap(appIcon.pixmap(64, 64));
        }
    }
    
    // Create text layout
    _textLayout = new QVBoxLayout();
    _textLayout->setSpacing(8);
    
    // Title label
    _titleLabel = new QLabel();
    _titleLabel->setText(QString("%1 %2").arg(geck::version::name).arg(geck::version::string));
    QFont titleFont = _titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 4);
    _titleLabel->setFont(titleFont);
    
    // Description label
    _descriptionLabel = new QLabel();
    _descriptionLabel->setText(geck::version::description);
    _descriptionLabel->setWordWrap(true);
    
    // Add copyright/build info
    QLabel* buildInfoLabel = new QLabel();
    buildInfoLabel->setText("Built with Qt6, SFML, and ❤️");
    buildInfoLabel->setStyleSheet("QLabel { color: #666; font-size: 10pt; }");
    
    _textLayout->addWidget(_titleLabel);
    _textLayout->addWidget(_descriptionLabel);
    _textLayout->addStretch();
    _textLayout->addWidget(buildInfoLabel);
    
    _contentLayout->addWidget(_iconLabel);
    _contentLayout->addLayout(_textLayout);
    
    _mainLayout->addLayout(_contentLayout);
}

void AboutDialog::createButtons() {
    _buttonLayout = new QHBoxLayout();
    _buttonLayout->addStretch();
    
    _okButton = new QPushButton("OK");
    _okButton->setDefault(true);
    _okButton->setMinimumWidth(80);
    
    connect(_okButton, &QPushButton::clicked, this, &QDialog::accept);
    
    _buttonLayout->addWidget(_okButton);
    _mainLayout->addLayout(_buttonLayout);
}

} // namespace geck