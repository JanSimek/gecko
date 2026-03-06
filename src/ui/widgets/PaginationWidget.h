#pragma once

#include <QWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QHBoxLayout>

namespace geck {

/**
 * @brief Reusable pagination widget for panels that display paged content
 *
 * This widget provides a consistent pagination interface with:
 * - Previous/Next buttons
 * - Page number spin box
 * - Page info label (e.g., "of 10")
 * - Signals for page changes
 */
class PaginationWidget : public QWidget {
    Q_OBJECT

public:
    explicit PaginationWidget(QWidget* parent = nullptr);

    /**
     * @brief Set the current page (1-based)
     */
    void setCurrentPage(int page);

    /**
     * @brief Set the total number of pages
     */
    void setTotalPages(int totalPages);

    /**
     * @brief Get the current page (1-based)
     */
    int getCurrentPage() const;

    /**
     * @brief Get the total number of pages
     */
    int getTotalPages() const;

    /**
     * @brief Enable or disable the pagination controls
     */
    void setEnabled(bool enabled);

    /**
     * @brief Set whether to show the navigation buttons
     */
    void setShowNavigationButtons(bool show);

    /**
     * @brief Set whether to show the first/last page buttons
     */
    void setShowFirstLastButtons(bool show);

    /**
     * @brief Go to the first page
     */
    void goToFirstPage();

    /**
     * @brief Go to the last page
     */
    void goToLastPage();

signals:
    /**
     * @brief Emitted when the user changes the page
     * @param page The new page number (1-based)
     */
    void pageChanged(int page);

    /**
     * @brief Emitted when the user requests to go to the first page
     */
    void firstPageRequested();

    /**
     * @brief Emitted when the user requests to go to the last page
     */
    void lastPageRequested();

private slots:
    void onFirstButtonClicked();
    void onPrevButtonClicked();
    void onNextButtonClicked();
    void onLastButtonClicked();
    void onPageSpinBoxChanged(int page);

private:
    void updateControls();

    // UI components
    QPushButton* _firstButton;
    QPushButton* _prevButton;
    QPushButton* _nextButton;
    QPushButton* _lastButton;
    QSpinBox* _pageSpinBox;
    QLabel* _pageInfoLabel;
    QHBoxLayout* _layout;

    // State
    int _currentPage;
    int _totalPages;
    bool _showNavigationButtons;
    bool _showFirstLastButtons;
};

} // namespace geck