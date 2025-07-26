#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <memory>

namespace geck {

class Loader;

class LoadingWidget : public QDialog {
    Q_OBJECT

public:
    explicit LoadingWidget(QWidget* parent = nullptr);
    ~LoadingWidget();

    void addLoader(std::unique_ptr<Loader> loader);
    void start();
    
    // Override exec() to auto-start loading
    int exec() override;

signals:
    void loadingComplete();

private slots:
    void updateProgress();

private:
    void setupUI();

    QVBoxLayout* _layout;
    QLabel* _titleLabel;
    QLabel* _statusLabel;
    QProgressBar* _progressBar;
    QTimer* _updateTimer;

    std::vector<std::unique_ptr<Loader>> _loaders;
    std::vector<bool> _loadersCompleted; // Track which loaders have had onDone() called
    bool _isLoading;
};

} // namespace geck