#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <memory>

namespace geck {

class Loader;

class LoadingWidget : public QWidget {
    Q_OBJECT

public:
    explicit LoadingWidget(QWidget* parent = nullptr);
    ~LoadingWidget();

    void addLoader(std::unique_ptr<Loader> loader);
    void start();

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
    bool _isLoading;
};

} // namespace geck