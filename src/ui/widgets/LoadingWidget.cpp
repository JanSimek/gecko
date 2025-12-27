#include "LoadingWidget.h"
#include "../../state/loader/Loader.h"
#include "../../util/Constants.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QFont>
#include <QApplication>
#include <spdlog/spdlog.h>

namespace geck {

LoadingWidget::LoadingWidget(QWidget* parent)
    : QDialog(parent)
    , _layout(nullptr)
    , _titleLabel(nullptr)
    , _statusLabel(nullptr)
    , _progressBar(nullptr)
    , _updateTimer(new QTimer(this))
    , _isLoading(false) {

    // Set up as modal dialog
    setModal(true);
    setWindowTitle("Loading");
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    setFixedSize(ui::constants::dialog_sizes::LOADING_WIDTH, ui::constants::dialog_sizes::LOADING_HEIGHT);

    setupUI();

    // Connect timer to update progress
    connect(_updateTimer, &QTimer::timeout, this, &LoadingWidget::updateProgress);

    // Auto-close when loading completes
    connect(this, &LoadingWidget::loadingComplete, this, &QDialog::accept);
}

LoadingWidget::~LoadingWidget() {
    if (_updateTimer && _updateTimer->isActive()) {
        _updateTimer->stop();
    }
}

void LoadingWidget::setupUI() {
    _layout = new QVBoxLayout(this);
    _layout->setContentsMargins(ui::constants::DIALOG_PADDING, ui::constants::DIALOG_PADDING,
        ui::constants::DIALOG_PADDING, ui::constants::DIALOG_PADDING);
    _layout->setSpacing(ui::constants::SPACING_WIDE);

    // Title label
    _titleLabel = new QLabel("Loading", this);
    _titleLabel->setFont(ui::theme::fonts::largeTitle());
    _titleLabel->setAlignment(Qt::AlignCenter);

    // Status label
    _statusLabel = new QLabel("Initializing...", this);
    _statusLabel->setFont(ui::theme::fonts::statusText());
    _statusLabel->setAlignment(Qt::AlignCenter);
    _statusLabel->setStyleSheet(ui::theme::styles::smallLabel());

    // Progress bar
    _progressBar = new QProgressBar(this);
    _progressBar->setMinimum(0);
    _progressBar->setMaximum(100);
    _progressBar->setValue(0);
    _progressBar->setTextVisible(true);

    // Style the progress bar for better visibility
    _progressBar->setStyleSheet(ui::theme::styles::progressBarStyle());

    // Add widgets to layout
    _layout->addWidget(_titleLabel);
    _layout->addWidget(_statusLabel);
    _layout->addSpacing(ui::constants::SPACING_WIDE);
    _layout->addWidget(_progressBar);
    _layout->addStretch();

    setLayout(_layout);
}

void LoadingWidget::addLoader(std::unique_ptr<Loader> loader) {
    _loaders.push_back(std::move(loader));
    _loadersCompleted.push_back(false); // Track completion status
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
    int totalProgress = 0;
    int activeLoaders = 0;

    for (size_t i = 0; i < _loaders.size(); ++i) {
        auto& loader = _loaders[i];

        if (!loader->isDone()) {
            allDone = false;
            activeLoaders++;

            // Update UI with current loader status
            _statusLabel->setText(QString::fromStdString(loader->status()));

            // Get actual progress percentage from loader
            int loaderProgress = loader->percentDone();
            totalProgress += loaderProgress;

            // Update progress bar with actual percentage
            if (activeLoaders == 1) {
                _progressBar->setValue(loaderProgress);
            }

            // Only show status from first active loader
            if (activeLoaders == 1) {
                std::string progressStr = loader->progress();
                if (!progressStr.empty()) {
                    _statusLabel->setText(QString::fromStdString(progressStr));
                }
            }
        } else {
            // Loader is done, execute completion callback once
            if (!_loadersCompleted[i]) {
                spdlog::debug("LoadingWidget: Calling onDone() for completed loader {}", i);
                loader->onDone();
                _loadersCompleted[i] = true;
            }
            totalProgress += 100; // Completed loaders contribute 100%
        }
    }

    // If multiple loaders, show average progress
    if (_loaders.size() > 1 && !allDone) {
        int averageProgress = totalProgress / static_cast<int>(_loaders.size());
        _progressBar->setValue(averageProgress);
    }

    if (allDone) {
        _updateTimer->stop();
        _isLoading = false;
        _progressBar->setValue(100);
        _statusLabel->setText("Complete");
        spdlog::info("LoadingWidget completed all loaders");

        // Emit signal after a short delay to show completion
        QTimer::singleShot(200, this, [this]() {
            emit loadingComplete();
        });
    }
}

int LoadingWidget::exec() {
    // Auto-start loading when exec() is called
    start();

    // Call parent exec() for modal behavior
    return QDialog::exec();
}

} // namespace geck