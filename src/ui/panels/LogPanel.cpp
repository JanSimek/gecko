#include "LogPanel.h"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QTreeView>
#include <QVBoxLayout>

#include "ui/logging/LogFilterProxy.h"
#include "ui/logging/LogModel.h"
#include "ui/theme/ThemeManager.h"

namespace geck {

LogPanel::LogPanel(QWidget* parent)
    : QWidget(parent)
    , _proxy(new LogFilterProxy(this))
    , _view(new QTreeView(this))
    , _levelFilter(new QComboBox(this))
    , _search(new QLineEdit(this))
    , _copyButton(new QPushButton(tr("Copy"), this))
    , _clearButton(new QPushButton(tr("Clear"), this)) {

    for (auto level : { LogModel::Level::Debug, LogModel::Level::Info, LogModel::Level::Warning, LogModel::Level::Error }) {
        _levelFilter->addItem(LogModel::levelName(level), static_cast<int>(level));
    }
    _levelFilter->setToolTip(tr("Show records at or above this level"));

    _search->setPlaceholderText(tr("Filter messages…"));
    _search->setClearButtonEnabled(true);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(ui::theme::spacing::NORMAL);
    controls->addWidget(_levelFilter);
    controls->addWidget(_search, 1);
    controls->addWidget(_copyButton);
    controls->addWidget(_clearButton);

    _view->setModel(_proxy);
    _view->setRootIsDecorated(false);
    _view->setUniformRowHeights(true);
    _view->setAlternatingRowColors(true);
    _view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _view->setSelectionBehavior(QAbstractItemView::SelectRows);
    _view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    _view->header()->setSectionResizeMode(LogModel::TimeColumn, QHeaderView::ResizeToContents);
    _view->header()->setSectionResizeMode(LogModel::LevelColumn, QHeaderView::ResizeToContents);
    _view->header()->setSectionResizeMode(LogModel::MessageColumn, QHeaderView::Stretch);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(ui::theme::spacing::TIGHT);
    layout->addLayout(controls);
    layout->addWidget(_view, 1);

    connect(_levelFilter, &QComboBox::currentIndexChanged, this, &LogPanel::onLevelFilterChanged);
    connect(_search, &QLineEdit::textChanged, this, &LogPanel::onSearchTextChanged);
    connect(_copyButton, &QPushButton::clicked, this, &LogPanel::onCopy);
    connect(_clearButton, &QPushButton::clicked, this, &LogPanel::onClear);

    const auto* copyShortcut = new QShortcut(QKeySequence::Copy, _view);
    connect(copyShortcut, &QShortcut::activated, this, &LogPanel::onCopy);

    // Follow the newest records unless the user has scrolled up to read something.
    connect(_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        _followTail = value >= _view->verticalScrollBar()->maximum();
    });
    connect(_proxy, &QAbstractItemModel::rowsInserted, this, [this]() {
        if (_followTail) {
            _view->scrollToBottom();
        }
    });
}

void LogPanel::setModel(LogModel* model) {
    _model = model;
    _proxy->setSourceModel(model);
}

void LogPanel::onLevelFilterChanged(int index) {
    _proxy->setMinimumLevel(static_cast<LogModel::Level>(_levelFilter->itemData(index).toInt()));
}

void LogPanel::onSearchTextChanged(const QString& text) {
    _proxy->setSearchText(text);
}

void LogPanel::onCopy() {
    QModelIndexList rows;
    const auto* selection = _view->selectionModel();
    if (selection && selection->hasSelection()) {
        rows = selection->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) { return a.row() < b.row(); });
    } else {
        rows.reserve(_proxy->rowCount());
        for (int row = 0; row < _proxy->rowCount(); ++row) {
            rows.append(_proxy->index(row, 0));
        }
    }

    QStringList lines;
    lines.reserve(rows.size());
    for (const QModelIndex& row : rows) {
        lines.append(QStringLiteral("[%1] [%2] %3")
                .arg(_proxy->index(row.row(), LogModel::TimeColumn).data().toString(),
                    _proxy->index(row.row(), LogModel::LevelColumn).data().toString(),
                    _proxy->index(row.row(), LogModel::MessageColumn).data().toString()));
    }

    if (!lines.isEmpty()) {
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    }
}

void LogPanel::onClear() {
    if (_model) {
        _model->clear();
    }
}

} // namespace geck
