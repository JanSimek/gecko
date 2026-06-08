#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "../format/frm/Frm.h"

namespace geck {
class Lst;
}

namespace geck::resource {

class ResourceRepository;

/// Maps an art/ path to its FRM type using the canonical directory prefixes.
/// Returns nullopt for paths that are not under a known art/ directory.
[[nodiscard]] std::optional<Frm::FRM_TYPE> frmTypeForArtPath(std::string_view path);

class FrmResolver final {
public:
    explicit FrmResolver(ResourceRepository& repository);

    [[nodiscard]] std::string resolve(uint32_t fid);

private:
    ResourceRepository& _repository;
};

} // namespace geck::resource
