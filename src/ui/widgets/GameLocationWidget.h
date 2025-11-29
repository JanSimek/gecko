#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QProgressBar>
#include <filesystem>
#include "../../util/Settings.h"

namespace geck {

/**
 * @brief Widget for managing game location configuration
 * 
 * Provides UI for configuring Steam vs Executable installations,
 * including separate paths for executables and game data directories.
 */
class GameLocationWidget : public QGroupBox {
    Q_OBJECT

public:
    explicit GameLocationWidget(QWidget* parent = nullptr);
    ~GameLocationWidget() = default;

    // Data access
    Settings::GameInstallationType getInstallationType() const;
    void setInstallationType(Settings::GameInstallationType type);
    
    std::string getSteamAppId() const;
    void setSteamAppId(const std::string& appId);
    
    std::filesystem::path getExecutableLocation() const;
    void setExecutableLocation(const std::filesystem::path& location);
    
    std::filesystem::path getDataDirectory() const;
    void setDataDirectory(const std::filesystem::path& location);
    
    // Status updates
    void setStatusMessage(const QString& message, const QString& styleClass = "normal");

signals:
    void installationTypeChanged();
    void configurationChanged();
    void statusChanged(const QString& message, const QString& styleClass);

private slots:
    void onInstallationTypeChanged();
    void onSteamAppIdChanged();
    void onExecutableLocationChanged();
    void onDataDirectoryChanged();
    void onBrowseExecutable();
    void onBrowseDataDirectory();
    void onAutoDetect();

private:
    void setupUI();
    void setupConnections();
    void updateControlStates();
    void validateGameLocation(const QString& gameDir, bool isSteam);
    
    // UI Components
    QVBoxLayout* _layout;
    QLabel* _helpLabel;
    
    // Steam installation
    QRadioButton* _steamRadio;
    QHBoxLayout* _steamLayout;
    QLabel* _steamAppIdLabel;
    QLineEdit* _steamAppIdEdit;
    QLabel* _steamHelpLabel;
    
    // Executable installation
    QRadioButton* _executableRadio;
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
