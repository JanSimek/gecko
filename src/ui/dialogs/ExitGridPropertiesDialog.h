#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>

namespace geck {

namespace resource {
    class MapNameResolver;
}

/**
 * @brief Properties dialog for exit grid configuration
 *
 * Provides UI for setting the 4 essential exit grid properties:
 * - Destination map ID
 * - Player spawn position (hex coordinate)
 * - Destination elevation level
 * - Player orientation/direction
 *
 * When a MapNameResolver is supplied, the destination map ID is annotated with the resolved .map
 * filename and friendly map.msg name (from maps.txt / map.msg), so the editor shows "arcaves.map ·
 * Temple: Foyer" instead of a bare number.
 */
class ExitGridPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    struct ExitGridProperties {
        // Which directional marker art to draw for a placed line. Auto picks each hex's art from the
        // line's local segment + its outward facing (exitGridArtForSegment); the explicit directions
        // force one art for every hex in the line — the escape hatch for ambiguous corners and for
        // diagonals, where the two sides of a "/" or "\" render near-identically. The map format has
        // no per-instance side field, so this is purely an art-proto choice, not a serialized value.
        // The eight explicit values map 1:1 onto ExitGrid::Direction (0..7).
        enum class MarkerArt {
            Auto,
            Left,     ///< dir 0 — vertical bar
            Right,    ///< dir 1 — vertical bar
            Bottom,   ///< dir 2 — horizontal bar
            Top,      ///< dir 3 — horizontal bar
            ForwardA, ///< dir 4 — "/" side A
            ForwardB, ///< dir 5 — "/" side B
            BackA,    ///< dir 6 — "\" side A
            BackB,    ///< dir 7 — "\" side B
        };

        uint32_t exitMap = 0;                  // Destination map ID
        uint32_t exitPosition = 0;             // Player spawn position (0-39999)
        uint32_t exitElevation = 0;            // Destination elevation (0-2)
        uint32_t exitOrientation = 0;          // Player direction (0-5)
        MarkerArt markerArt = MarkerArt::Auto; // Directional art override (not serialized)
    };

    explicit ExitGridPropertiesDialog(QWidget* parent = nullptr, const resource::MapNameResolver* names = nullptr);
    explicit ExitGridPropertiesDialog(const ExitGridProperties& properties, QWidget* parent = nullptr,
        const resource::MapNameResolver* names = nullptr);
    ~ExitGridPropertiesDialog() = default;

    ExitGridProperties getProperties() const;
    void setProperties(const ExitGridProperties& properties);

public slots:
    void accept() override;

private slots:
    void onPositionChanged();
    void validateInput();
    void updateMapName();
    void onExitToWorldmapToggled(bool checked);
    void onTownMapToggled(bool checked);

private:
    void setupUI();
    void setupFormLayout();
    void setupButtonBox();
    void updateUI();
    void updateMapControlsEnabled();
    bool isValidInput() const;

    // UI Components
    QVBoxLayout* _mainLayout;
    QFormLayout* _formLayout;
    QDialogButtonBox* _buttonBox;

    // Input fields
    QCheckBox* _exitToWorldmapCheckBox;
    QCheckBox* _townMapCheckBox;
    QSpinBox* _mapIdSpinBox;
    QSpinBox* _positionSpinBox;
    QComboBox* _elevationComboBox;
    QComboBox* _orientationComboBox;
    QComboBox* _markerArtComboBox;

    // Resolved destination-map name shown under the map ID (filename · friendly name)
    QLabel* _mapNameLabel;

    // Status
    QLabel* _statusLabel;

    // Resolves a map ID to its .map filename + map.msg name (not owned; may be null)
    const resource::MapNameResolver* _names;

    // Properties
    ExitGridProperties _properties;
};

} // namespace geck