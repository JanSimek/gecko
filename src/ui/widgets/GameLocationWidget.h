#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QProgressBar>
#include <filesystem>

namespace geck {

/**
 * @brief Widget for managing the Fallout 2 game location.
 *
 * Configures the game executable and its data directory, which the Play feature
 * uses to launch the game with the currently edited map.
 */
class GameLocationWidget : public QGroupBox {
    Q_OBJECT

public:
    explicit GameLocationWidget(QWidget* parent = nullptr);
    ~GameLocationWidget() = default;

    // Data access
    std::filesystem::path getExecutableLocation() const;
    void setExecutableLocation(const std::filesystem::path& location);

    std::filesystem::path getDataDirectory() const;
    void setDataDirectory(const std::filesystem::path& location);

    // Status updates
    void setStatusMessage(const QString& message, const QString& styleClass = "normal");

signals:
    void configurationChanged();
    void statusChanged(const QString& message, const QString& styleClass);

private slots:
    void onExecutableLocationChanged();
    void onDataDirectoryChanged();
    void onBrowseExecutable();
    void onBrowseDataDirectory();
    void onAutoDetect();

private:
    void setupUI();
    void setupConnections();
    void validateGameLocation(const QString& gamePath);
    void validateExecutableFile(const std::filesystem::path& path);
    void validateInstallDirectory(const std::filesystem::path& path);

    // UI Components
    QVBoxLayout* _layout;
    QLabel* _helpLabel;

    // Executable installation
    QLabel* _executableLabel;
    QHBoxLayout* _executableLayout;
    QLineEdit* _executableLocationEdit;
    QPushButton* _browseExecutableButton;

    // Game data directory
    QLabel* _dataDirectoryLabel;
    QHBoxLayout* _dataDirectoryLayout;
    QLineEdit* _dataDirectoryEdit;
    QPushButton* _browseDataDirectoryButton;

    // Controls
    QHBoxLayout* _controlLayout;
    QPushButton* _autoDetectButton;
    QProgressBar* _progressBar;
};

} // namespace geck
