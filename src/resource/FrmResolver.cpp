#include "FrmResolver.h"

#include "ResourceRepository.h"

#include "format/frm/Frm.h"
#include "format/frm/FrmId.h"
#include "format/lst/Lst.h"
#include "util/Constants.h"
#include "resource/CritterFrmResolver.h"
#include "resource/ResourcePaths.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
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

    std::string trimmed(std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        return std::string(text.substr(first, last - first + 1));
    }

    bool equalsIgnoreCase(std::string_view a, std::string_view b) {
        return a.size() == b.size()
            && std::equal(a.begin(), a.end(), b.begin(), [](char lhs, char rhs) {
                   return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
               });
    }

} // namespace

bool hasFrmExtension(std::string_view filename) {
    if (filename.size() < 4) {
        return false;
    }
    std::string ext = trimmed(filename.substr(filename.size() - 4));
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return ext == ".frm" || ext == ".fr0" || ext == ".fr1" || ext == ".fr2"
        || ext == ".fr3" || ext == ".fr4" || ext == ".fr5";
}

std::optional<ObjectType> frmTypeForArtPath(std::string_view path) {
    // frmTypeDescriptions is indexed in ObjectType order, so the index is the type.
    for (size_t i = 0; i < frmTypeDescriptions.size(); ++i) {
        if (path.find(frmTypeDescriptions[i].prefixPath) != std::string_view::npos) {
            return static_cast<ObjectType>(i);
        }
    }
    return std::nullopt;
}

FrmResolver::FrmResolver(ResourceRepository& repository)
    : _repository(repository) {
}

std::string FrmResolver::resolve(uint32_t fid) {
    const FrmId frmId{ fid };
    const ObjectType type = frmId.objectType();
    const uint32_t baseId = frmId.frameId();

    if (type == ObjectType::Misc && baseId == WallBlockers::SCROLL_BLOCKER_FID.frameId()) {
        return std::string(ResourcePaths::Frm::SCROLL_BLOCKER);
    }

    if (type == ObjectType::Invalid || type > ObjectType::Inventory) {
        throw std::runtime_error("Invalid FID object type");
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
    if (type == ObjectType::Critter) {
        return std::string(typeDescription.prefixPath) + frmName.substr(0, 6) + Frm::STANDING_ANIMATION_SUFFIX;
    }

    return std::string(typeDescription.prefixPath) + frmName;
}

std::optional<uint32_t> FrmResolver::resolveFid(const std::string& artPath) {
    if (artPath.empty()) {
        return std::nullopt;
    }

    std::string normalized = artPath;
    if (normalized.front() == '/') {
        normalized.erase(0, 1);
    }
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    const size_t lastSlash = normalized.find_last_of('/');
    const std::string filename = (lastSlash != std::string::npos) ? normalized.substr(lastSlash + 1) : normalized;

    if (!hasFrmExtension(filename)) {
        return std::nullopt;
    }

    for (size_t typeIndex = 0; typeIndex < frmTypeDescriptions.size(); ++typeIndex) {
        const auto& description = frmTypeDescriptions[typeIndex];
        if (!normalized.starts_with(description.prefixPath)) {
            continue;
        }

        auto* lst = _repository.load<Lst>(std::string(description.lstFilePath));
        if (!lst) {
            return std::nullopt;
        }

        const auto type = static_cast<ObjectType>(typeIndex);
        const auto& entries = lst->list();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (type == ObjectType::Critter) {
                const auto commaPos = entries[i].find(',');
                const std::string baseName = trimmed(
                    commaPos != std::string::npos ? std::string_view(entries[i]).substr(0, commaPos) : std::string_view(entries[i]));
                if (!baseName.empty() && CritterFrmResolver::matchesCritterBase(baseName, filename)) {
                    const uint32_t fid = CritterFrmResolver::deriveCritterFrmPid(baseName, filename, static_cast<uint32_t>(i));
                    if (fid != 0) {
                        return fid;
                    }
                }
                continue;
            }

            const std::string entry = trimmed(entries[i]);
            if (!entry.empty() && equalsIgnoreCase(entry, filename)) {
                // A FID holds a 12-bit frame id, so a later entry has no FID that names it.
                if (i > FrmId::MAX_FRAME_ID) {
                    return std::nullopt;
                }
                return FrmId(type, static_cast<uint32_t>(i)).fid();
            }
        }

        // Path is under a known art directory but absent from its LST.
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace geck::resource
