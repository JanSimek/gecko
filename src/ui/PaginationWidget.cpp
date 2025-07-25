#include "PaginationWidget.h"
#include <QHBoxLayout>

namespace geck {

PaginationWidget::PaginationWidget(QWidget* parent)
    : QWidget(parent)
    , _currentPage(1)
    , _totalPages(1)
    , _showNavigationButtons(true)
    , _showFirstLastButtons(true) {

    _layout = new QHBoxLayout(this);
    _layout->setContentsMargins(0, 0, 0, 0);

    // First page button
    _firstButton = new QPushButton("|◀", this);
    _firstButton->setMaximumWidth(30);
    _firstButton->setToolTip("Go to first page");
    connect(_firstButton, &QPushButton::clicked, this, &PaginationWidget::onFirstButtonClicked);
    _layout->addWidget(_firstButton);

    // Previous button
    _prevButton = new QPushButton("◀", this);
    _prevButton->setMaximumWidth(30);
    _prevButton->setToolTip("Previous page");
    connect(_prevButton, &QPushButton::clicked, this, &PaginationWidget::onPrevButtonClicked);
    _layout->addWidget(_prevButton);

    // Page selector
    _layout->addWidget(new QLabel("Page:", this));
    _pageSpinBox = new QSpinBox(this);
    _pageSpinBox->setMinimum(1);
    _pageSpinBox->setMaximum(1);
    _pageSpinBox->setValue(1);
    _pageSpinBox->setMaximumWidth(60);
    connect(_pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &PaginationWidget::onPageSpinBoxChanged);
    _layout->addWidget(_pageSpinBox);

    _pageInfoLabel = new QLabel("of 1", this);
    _layout->addWidget(_pageInfoLabel);

    // Next button
    _nextButton = new QPushButton("▶", this);
    _nextButton->setMaximumWidth(30);
    _nextButton->setToolTip("Next page");
    connect(_nextButton, &QPushButton::clicked, this, &PaginationWidget::onNextButtonClicked);
    _layout->addWidget(_nextButton);

    // Last page button
    _lastButton = new QPushButton("▶|", this);
    _lastButton->setMaximumWidth(30);
    _lastButton->setToolTip("Go to last page");
    connect(_lastButton, &QPushButton::clicked, this, &PaginationWidget::onLastButtonClicked);
    _layout->addWidget(_lastButton);

    // Add stretch to push controls to the left
    _layout->addStretch();

    updateControls();
}

void PaginationWidget::setCurrentPage(int page) {
    if (page < 1 || page > _totalPages) {
        return;
    }

    _currentPage = page;
    _pageSpinBox->blockSignals(true);
    _pageSpinBox->setValue(_currentPage);
    _pageSpinBox->blockSignals(false);
    updateControls();
}

void PaginationWidget::setTotalPages(int totalPages) {
    if (totalPages < 1) {
        totalPages = 1;
    }

    _totalPages = totalPages;
    _pageSpinBox->setMaximum(_totalPages);
    
    // Adjust current page if it's now out of range
    if (_currentPage > _totalPages) {
        setCurrentPage(_totalPages);
    }

    _pageInfoLabel->setText(QString("of %1").arg(_totalPages));
    updateControls();
}

int PaginationWidget::getCurrentPage() const {
    return _currentPage;
}

int PaginationWidget::getTotalPages() const {
    return _totalPages;
}

void PaginationWidget::setEnabled(bool enabled) {
    _firstButton->setEnabled(enabled);
    _prevButton->setEnabled(enabled);
    _nextButton->setEnabled(enabled);
    _lastButton->setEnabled(enabled);
    _pageSpinBox->setEnabled(enabled);
    QWidget::setEnabled(enabled);
}

void PaginationWidget::setShowNavigationButtons(bool show) {
    _showNavigationButtons = show;
    _prevButton->setVisible(show);
    _nextButton->setVisible(show);
}

void PaginationWidget::setShowFirstLastButtons(bool show) {
    _showFirstLastButtons = show;
    _firstButton->setVisible(show);
    _lastButton->setVisible(show);
}

void PaginationWidget::goToFirstPage() {
    if (_currentPage != 1) {
        setCurrentPage(1);
        emit pageChanged(_currentPage);
    }
}

void PaginationWidget::goToLastPage() {
    if (_currentPage != _totalPages) {
        setCurrentPage(_totalPages);
        emit pageChanged(_currentPage);
    }
}

void PaginationWidget::onFirstButtonClicked() {
    goToFirstPage();
    emit firstPageRequested();
}

void PaginationWidget::onPrevButtonClicked() {
    if (_currentPage > 1) {
        setCurrentPage(_currentPage - 1);
        emit pageChanged(_currentPage);
    }
}

void PaginationWidget::onNextButtonClicked() {
    if (_currentPage < _totalPages) {
        setCurrentPage(_currentPage + 1);
        emit pageChanged(_currentPage);
    }
}

void PaginationWidget::onLastButtonClicked() {
    goToLastPage();
    emit lastPageRequested();
}

void PaginationWidget::onPageSpinBoxChanged(int page) {
    if (page != _currentPage) {
        _currentPage = page;
        updateControls();
        emit pageChanged(_currentPage);
    }
}

void PaginationWidget::updateControls() {
    // Update button states
    _firstButton->setEnabled(_currentPage > 1 && _totalPages > 1);
    _prevButton->setEnabled(_currentPage > 1 && _totalPages > 1);
    _nextButton->setEnabled(_currentPage < _totalPages && _totalPages > 1);
    _lastButton->setEnabled(_currentPage < _totalPages && _totalPages > 1);
    
    // Update spin box state
    _pageSpinBox->setEnabled(_totalPages > 1);
}

} // namespace geck