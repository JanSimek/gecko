#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "format/frm/Frm.h"

namespace geck {
class Lst;
}

namespace geck::resource {

class ResourceRepository;

/// Maps an art/ path to its FRM type using the canonical directory prefixes.
/// Returns nullopt for paths that are not under a known art/ directory.
[[nodiscard]] std::optional<Frm::FRM_TYPE> frmTypeForArtPath(std::string_view path);

/// True if the filename ends in a Fallout FRM extension: the standard ".frm" or a
/// directional ".fr0"-".fr5" (used by split critter animations). This is the
/// canonical FRM-extension test; reuse it instead of re-deriving the extension set.
[[nodiscard]] bool hasFrmExtension(std::string_view filename);

class FrmResolver final {
public:
    explicit FrmResolver(ResourceRepository& repository);

    /// Resolves a FID to its art path (LST lookup, type byte = FRM_TYPE).
    [[nodiscard]] std::string resolve(uint32_t fid);

    /// Inverse of resolve(): derives the FID for an art/ path by locating its
    /// filename in the matching LST. The FID type byte is the FRM_TYPE ordinal,
    /// matching the engine (OBJ_TYPE) and resolve(). Returns nullopt when the
    /// path is not under a known art/ directory or is absent from its LST; there
    /// is no heuristic fallback. Critter resolution is lossy (animation-encoded),
    /// so resolve(resolveFid(p)) is not guaranteed to round-trip for critters.
    [[nodiscard]] std::optional<uint32_t> resolveFid(const std::string& artPath);

private:
    ResourceRepository& _repository;
};

} // namespace geck::resource
