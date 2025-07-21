#include "LoadingWidget.h"
#include "../state/loader/Loader.h"
#include "../util/Constants.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QFont>
#include <spdlog/spdlog.h>

namespace geck {

LoadingWidget::LoadingWidget(QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _titleLabel(nullptr)
    , _statusLabel(nullptr)
    , _progressBar(nullptr)
    , _updateTimer(new QTimer(this))
    , _isLoading(false) {

    setupUI();

    // Connect timer to update progress
    connect(_updateTimer, &QTimer::timeout, this, &LoadingWidget::updateProgress);
}

LoadingWidget::~LoadingWidget() {
    if (_updateTimer && _updateTimer->isActive()) {
        _updateTimer->stop();
    }
}

void LoadingWidget::setupUI() {
    _layout = new QVBoxLayout(this);
    _layout->setAlignment(Qt::AlignCenter);

    // Title label
    _titleLabel = new QLabel("Loading", this);
    QFont titleFont = _titleLabel->font();
    titleFont.setPointSize(UI::TITLE_FONT_SIZE);
    titleFont.setBold(true);
    _titleLabel->setFont(titleFont);
    _titleLabel->setAlignment(Qt::AlignCenter);
    _titleLabel->setStyleSheet("color: white;");

    // Status label
    _statusLabel = new QLabel("Initializing...", this);
    QFont statusFont = _statusLabel->font();
    statusFont.setPointSize(UI::STATUS_FONT_SIZE);
    _statusLabel->setFont(statusFont);
    _statusLabel->setAlignment(Qt::AlignCenter);
    _statusLabel->setStyleSheet("color: white;");

    // Progress bar
    _progressBar = new QProgressBar(this);
    _progressBar->setMinimum(UI::PROGRESS_BAR_MIN);
    _progressBar->setMaximum(UI::PROGRESS_BAR_MAX);
    _progressBar->setValue(UI::PROGRESS_BAR_MIN);

    // Add widgets to layout
    _layout->addStretch();
    _layout->addWidget(_titleLabel);
    _layout->addSpacing(UI::SPACING_LARGE);
    _layout->addWidget(_statusLabel);
    _layout->addSpacing(UI::SPACING_SMALL);
    _layout->addWidget(_progressBar);
    _layout->addStretch();

    // Set background color
    setStyleSheet("background-color: black;");

    setLayout(_layout);
}

void LoadingWidget::addLoader(std::unique_ptr<Loader> loader) {
    _loaders.push_back(std::move(loader));
}

void LoadingWidget::start() {
    if (_loaders.empty()) {
        spdlog::warn("LoadingWidget::start() called with no loaders");
        emit loadingComplete();
        return;
    }

    _isLoading = true;

    // Initialize all loaders
    for (const auto& loader : _loaders) {
        loader->init();
    }

    // Start update timer (30 FPS for smooth progress updates)
    _updateTimer->start(UI::TIMER_INTERVAL_MS);

    spdlog::info("LoadingWidget started with {} loaders", _loaders.size());
}

void LoadingWidget::updateProgress() {
    if (!_isLoading) {
        return;
    }

    bool allDone = true;

    for (const auto& loader : _loaders) {
        if (!loader->isDone()) {
            allDone = false;

            // Update UI with current loader status
            _statusLabel->setText(QString::fromStdString(loader->status()));

            // Try to extract progress percentage from progress string
            std::string progressStr = loader->progress();
            _statusLabel->setText(QString::fromStdString(progressStr));

            // For now, just use indeterminate progress
            // TODO: Implement actual progress percentage in loaders
            break;
        } else {
            // Execute completion callback
            loader->onDone();
        }
    }

    if (allDone) {
        _updateTimer->stop();
        _isLoading = false;
        spdlog::info("LoadingWidget completed all loaders");
        emit loadingComplete();
    }
}

} // namespace geck