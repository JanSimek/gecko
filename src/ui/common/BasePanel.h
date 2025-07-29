#pragma once

#include "BaseWidget.h"
#include "../widgets/PaginationWidget.h"
#include <QGroupBox>
#include <QScrollArea>
#include <QLineEdit>
#include <memory>

namespace geck {

/**
 * @brief Base class for panel widgets
 * 
 * Provides common functionality for panels like ObjectPalettePanel
 * and TilePalettePanel, following DRY principle.
 */
class BasePanel : public BaseWidget {
    Q_OBJECT

public:
    explicit BasePanel(const QString& title, QWidget* parent = nullptr)
        : BaseWidget(parent)
        , _panelTitle(title) {}

    virtual ~BasePanel() = default;

protected:
    /**
     * @brief Pure virtual function to set up the UI
     * Derived classes must implement their specific UI setup
     */
    virtual void setupUI() = 0;

    /**
     * @brief Creates standard search controls
     * @return The created search line edit
     */
    [[nodiscard]] QLineEdit* createSearchControls(const QString& placeholder = "Search...") {
        auto* searchGroup = new QGroupBox("Search", this);
        auto* searchLayout = new QVBoxLayout(searchGroup);
        
        _searchLineEdit = new QLineEdit(this);
        _searchLineEdit->setPlaceholderText(placeholder);
        _searchLineEdit->setClearButtonEnabled(true);
        
        searchLayout->addWidget(_searchLineEdit);
        
        if (_mainLayout) {
            _mainLayout->addWidget(searchGroup);
        }
        
        return _searchLineEdit;
    }

    /**
     * @brief Creates standard pagination controls
     * @param itemsPerPage Number of items per page
     * @return The created pagination widget
     */
    [[nodiscard]] PaginationWidget* createPaginationControls(int itemsPerPage) {
        _paginationWidget = new PaginationWidget(this);
        _itemsPerPage = itemsPerPage; // Store for derived classes to use
        
        if (_mainLayout) {
            _mainLayout->addWidget(_paginationWidget);
        }
        
        return _paginationWidget;
    }

    /**
     * @brief Creates a standard scroll area with grid layout
     * @return The created scroll area
     */
    [[nodiscard]] QScrollArea* createScrollArea() {
        _scrollArea = new QScrollArea(this);
        _scrollArea->setWidgetResizable(true);
        _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        
        _scrollContent = new QWidget();
        _gridLayout = new QGridLayout(_scrollContent);
        _gridLayout->setSpacing(4);
        
        _scrollArea->setWidget(_scrollContent);
        
        if (_mainLayout) {
            _mainLayout->addWidget(_scrollArea, 1); // Stretch factor 1
        }
        
        return _scrollArea;
    }

    /**
     * @brief Updates grid columns based on widget width
     * @param widgetSize Size of individual widgets
     * @param spacing Spacing between widgets
     */
    void updateGridColumns(int widgetSize, int spacing = 8) {
        if (!_scrollArea || !_gridLayout) return;
        
        const int availableWidth = _scrollArea->viewport()->width();
        const int effectiveWidgetSize = widgetSize + spacing;
        
        _gridColumns = std::max(1, availableWidth / effectiveWidgetSize);
    }

    // Protected members accessible to derived classes
    QVBoxLayout* _mainLayout = nullptr;
    QLineEdit* _searchLineEdit = nullptr;
    PaginationWidget* _paginationWidget = nullptr;
    QScrollArea* _scrollArea = nullptr;
    QWidget* _scrollContent = nullptr;
    QGridLayout* _gridLayout = nullptr;
    
    QString _panelTitle;
    int _gridColumns = 4;
    int _currentPage = 0;
    int _itemsPerPage = 100; // Default items per page

protected slots:
    /**
     * @brief Called when search text changes
     * Derived classes should override to implement filtering
     */
    virtual void onSearchTextChanged(const QString& text) = 0;

    /**
     * @brief Called when page changes
     * Derived classes should override to update displayed items
     */
    virtual void onPageChanged(int page) {
        _currentPage = page;
    }
};

} // namespace geck