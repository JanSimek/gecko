#include "SceneryDestinationDialog.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

#include "editor/HexagonGrid.h"
#include "util/BuiltTile.h"

namespace geck {

namespace {
    constexpr int MAX_HEX_TILE = HexagonGrid::POSITION_COUNT - 1;
} // namespace

SceneryDestinationDialog::SceneryDestinationDialog(Pro::SCENERY_TYPE sceneryType,
    uint32_t elevhex, uint32_t map, uint32_t elevtype, uint32_t elevlevel, QWidget* parent)
    : BaseDialog("Scenery Destination", parent)
    , _isElevator(sceneryType == Pro::SCENERY_TYPE::ELEVATOR)
    , _elevhex(elevhex)
    , _map(map)
    , _elevtype(elevtype)
    , _elevlevel(elevlevel) {

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    if (_isElevator) {
        _elevTypeSpin = new QSpinBox(this);
        _elevTypeSpin->setRange(-1, 32000);
        _elevTypeSpin->setValue(static_cast<int>(elevtype));
        formLayout->addRow("Elevator type:", _elevTypeSpin);

        _elevLevelSpin = new QSpinBox(this);
        _elevLevelSpin->setRange(-1, 32000);
        _elevLevelSpin->setValue(static_cast<int>(elevlevel));
        formLayout->addRow("Elevator level:", _elevLevelSpin);
    } else {
        const int tile = static_cast<int>(built_tile::tileOf(elevhex));
        const int elevation = static_cast<int>(built_tile::elevationOf(elevhex));

        _tileSpin = new QSpinBox(this);
        _tileSpin->setRange(0, MAX_HEX_TILE);
        _tileSpin->setValue(tile > MAX_HEX_TILE ? 0 : tile);
        formLayout->addRow("Destination tile:", _tileSpin);

        _elevationSpin = new QSpinBox(this);
        _elevationSpin->setRange(0, 2);
        _elevationSpin->setValue(elevation > 2 ? 0 : elevation);
        formLayout->addRow("Destination elevation:", _elevationSpin);

        _mapSpin = new QSpinBox(this);
        _mapSpin->setRange(0, 32000);
        _mapSpin->setValue(static_cast<int>(map));
        formLayout->addRow("Destination map:", _mapSpin);
    }

    mainLayout->addLayout(formLayout);

    mainLayout->addWidget(createButtonBox());
}

uint32_t SceneryDestinationDialog::getElevhex() const {
    if (_isElevator) {
        return _elevhex;
    }
    return built_tile::create(static_cast<uint32_t>(_tileSpin->value()),
        static_cast<uint32_t>(_elevationSpin->value()));
}

uint32_t SceneryDestinationDialog::getMap() const {
    return _isElevator ? _map : static_cast<uint32_t>(_mapSpin->value());
}

uint32_t SceneryDestinationDialog::getElevtype() const {
    return _isElevator ? static_cast<uint32_t>(_elevTypeSpin->value()) : _elevtype;
}

uint32_t SceneryDestinationDialog::getElevlevel() const {
    return _isElevator ? static_cast<uint32_t>(_elevLevelSpin->value()) : _elevlevel;
}

} // namespace geck
