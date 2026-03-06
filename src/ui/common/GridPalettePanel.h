#pragma once

#include "BasePanel.h"
#include "../UIConstants.h"
#include <QScrollArea>
#include <QGridLayout>

namespace geck {

/**
 * @brief Base class for palette panels with paginated grid layouts
 *
 * Provides common functionality for TilePalettePanel and ObjectPalettePanel:
 * - Grid layout with configurable columns
 * - Pagination with automatic page calculation
 * - Viewport-aware column optimization
 * - Widget clear/refresh pattern
 *
 * Following DRY principle by consolidating duplicate code from both panels.
 */
class GridPalettePanel : public BasePanel {
    Q_OBJECT

public:
    explicit GridPalettePanel(const QString& title, QWidget* parent = nullptr);
    virtual ~GridPalettePanel() = default;

protected:
    /**
     * @brief Sets up the grid scroll area with standard configuration
     * Call this from derived class setupUI() instead of creating scroll area manually
     */
    void setupGridArea();

    /**
     * @brief Clears all widgets from the grid layout safely
     */
    void clearGridWidgets();

    /**
     * @brief Updates pagination state based on filtered item count
     * @param filteredCount Total number of items after filtering
     */
    void updatePaginationState(int filteredCount);

    /**
     * @brief Calculates and updates pagination controls visibility
     */
    void updatePaginationControls();

    /**
     * @brief Gets the starting index for items on the current page
     * @return Index of first item to display (0-based)
     */
    int getPageStartIndex() const;

    /**
     * @brief Gets the ending index for items on the current page
     * @return Index of last item to display (0-based, inclusive)
     */
    int getPageEndIndex() const;

    /**
     * @brief Calculates optimal column count based on viewport width and item size
     * @param itemSize Size of individual grid items (width in pixels)
     * @return Optimal number of columns
     */
    int calculateOptimalColumnsPerRow(int itemSize) const;

    /**
     * @brief Gets the default number of columns for this panel type
     * Derived classes must override to return their preferred default
     * @return Default column count
     */
    virtual int getDefaultColumnsPerRow() const = 0;

    /**
     * @brief Called when grid needs to be updated (override in derived classes)
     */
    virtual void updateGrid() = 0;

    // Accessors for derived classes
    QGridLayout* gridLayout() const { return _gridLayout; }
    QScrollArea* scrollArea() const { return _scrollArea; }
    QWidget* gridWidget() const { return _gridWidget; }

    int currentPage() const { return _currentPage; }
    int totalPages() const { return _totalPages; }
    int totalFilteredItems() const { return _totalFilteredItems; }
    int columnsPerRow() const { return _columnsPerRow; }

    void setColumnsPerRow(int columns) { _columnsPerRow = columns; }

    // Resize event handling
    void resizeEvent(QResizeEvent* event) override;

protected slots:
    /**
     * @brief Handles pagination page changes
     * @param page New page number (1-based from PaginationWidget)
     */
    void onGridPaginationPageChanged(int page);

protected:
    // Grid components (managed by this class)
    QScrollArea* _scrollArea = nullptr;
    QWidget* _gridWidget = nullptr;
    QGridLayout* _gridLayout = nullptr;

    // Pagination group box for visibility control
    QGroupBox* _paginationGroup = nullptr;

    // Pagination state
    int _currentPage = 0;
    int _totalPages = 0;
    int _totalFilteredItems = 0;
    int _columnsPerRow = 6;
    int _previousColumnsPerRow = -1;
};

} // namespace geck
