#include "FrmResolver.h"

#include "ResourceRepository.h"

#include "../format/frm/Frm.h"
#include "../format/lst/Lst.h"
#include "../util/Constants.h"
#include "../util/ResourcePaths.h"

#include <stdexcept>
#include <string_view>

namespace geck::resource {
namespace {

    struct TypeArtListDescription {
        std::string_view prefixPath;
        std::string_view lstFilePath;
    };

    constexpr std::array<TypeArtListDescription, 8> frmTypeDescriptions = { {
        { ResourcePaths::Directories::ITEMS, ResourcePaths::Lst::ITEMS },
        { ResourcePaths::Directories::CRITTERS, ResourcePaths::Lst::CRITTERS },
        { ResourcePaths::Directories::SCENERY, ResourcePaths::Lst::SCENERY },
        { ResourcePaths::Directories::WALLS, ResourcePaths::Lst::WALLS },
        { ResourcePaths::Directories::TILES, ResourcePaths::Lst::TILES },
        { ResourcePaths::Directories::MISC, ResourcePaths::Lst::MISC },
        { ResourcePaths::Directories::INTERFACE, ResourcePaths::Lst::INTERFACE },
        { ResourcePaths::Directories::INVENTORY, ResourcePaths::Lst::INVENTORY },
    } };

} // namespace

FrmResolver::FrmResolver(ResourceRepository& repository)
    : _repository(repository) {
}

std::string FrmResolver::resolve(uint32_t fid) {
    auto baseId = fid & FileFormat::BASE_ID_MASK;
    auto type = static_cast<Frm::FRM_TYPE>(fid >> FileFormat::TYPE_MASK_SHIFT);

    if (type == Frm::FRM_TYPE::CRITTER) {
        baseId = fid & FileFormat::CRITTER_ID_MASK;
        type = static_cast<Frm::FRM_TYPE>((fid & FileFormat::TYPE_MASK) >> FileFormat::TYPE_MASK_SHIFT);
    }

    if (type == Frm::FRM_TYPE::MISC && baseId == WallBlockers::SCROLL_BLOCKER_BASE_ID) {
        return std::string(ResourcePaths::Frm::SCROLL_BLOCKER);
    }

    if (type == Frm::FRM_TYPE::WALL && baseId == 620) {
        return std::string(ResourcePaths::Frm::WALL_BLOCK);
    }

    if (type > Frm::FRM_TYPE::INVENTORY) {
        throw std::runtime_error("Invalid FRM_TYPE");
    }

    const auto& typeDescription = frmTypeDescriptions[static_cast<size_t>(type)];
    auto* lst = _repository.load<Lst>(typeDescription.lstFilePath);
    if (!lst) {
        throw std::runtime_error(std::string("Failed to load LST resource: ") + std::string(typeDescription.lstFilePath));
    }

    if (baseId >= lst->list().size()) {
        throw std::runtime_error(
            std::string("LST ") + std::string(typeDescription.lstFilePath) + " size " + std::to_string(lst->list().size())
            + " <= frmID: " + std::to_string(baseId) + ", frmType: " + std::to_string(static_cast<unsigned>(type)));
    }

    std::string frmName = lst->list().at(baseId);
    if (type == Frm::FRM_TYPE::CRITTER) {
        return std::string(typeDescription.prefixPath) + frmName.substr(0, 6) + Frm::STANDING_ANIMATION_SUFFIX;
    }

    return std::string(typeDescription.prefixPath) + frmName;
}

} // namespace geck::resource
