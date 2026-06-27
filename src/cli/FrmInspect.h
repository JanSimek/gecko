#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>

namespace geck {

namespace resource {
    class GameResources;
}

namespace cli {

    /// Options for `frm render`: turn one FRM (named or by FID) into a PNG.
    struct FrmRenderOptions {
        /// An art name (ext2grd1, art/misc/ext2grd1.frm) OR a FID (0x05000021 / decimal).
        std::string target;
        std::string outPath;
        /// Render a single direction (0-5) only; <0 means all six.
        int direction = -1;
        /// Render a single frame index only; <0 means every frame of each rendered direction.
        int frame = -1;
    };

    /// Parse a numeric FID written as hex (0x...) or decimal. nullopt when the whole token is not a
    /// valid number (so a caller can fall back to treating the token as an art name).
    [[nodiscard]] std::optional<uint32_t> parseFid(const std::string& token);

    /// Normalize an art name to a canonical `art/<dir>/<name>.frm` path: strips a leading slash,
    /// flips backslashes, lowercases, and adds the `.frm` suffix when the token has no FRM extension.
    /// A bare name with no directory (e.g. "ext2grd1") cannot be placed under an art subdir on its own
    /// and is returned unchanged (with the extension added) — resolution then locates it via the LSTs.
    [[nodiscard]] std::string normalizeArtToken(const std::string& token);

    /// Resolve a `frm` target (a FID or an art name) to its `art/...frm` path. FIDs go through
    /// FrmResolver::resolve(); names are normalized and, when they carry no directory, searched across
    /// every art LST. Returns nullopt (and leaves `error` set) when nothing matches.
    [[nodiscard]] std::optional<std::string> resolveFrmTarget(
        resource::GameResources& resources, const std::string& target, std::string& error);

    /// `frm info`: emit JSON metadata (resolvedArtPath, fid, directionCount, framesPerDirection, and a
    /// per-frame array of {direction,frame,width,height,offsetX,offsetY}) for the named/FID target.
    /// Returns 0 on success; nonzero with a message to `out` when the target can't be resolved/read.
    int frmInfo(resource::GameResources& resources, const std::string& target, std::ostream& out);

    /// `frm render`: render the FRM to a PNG. Default is a labelled grid of all directions x frames on
    /// a checkerboard; --dir / --frame narrow it. Returns 0 on success; nonzero with a message to `out`
    /// on failure — unresolvable target, unreadable FRM, no off-screen GL context, or write failure.
    int frmRender(resource::GameResources& resources, const FrmRenderOptions& options, std::ostream& out);

    /// `frm resolve <fid>`: emit JSON {fid, type, index, artPath} decoded from a FID.
    /// Returns 0 on success; nonzero with a message to `out` for a bad FID or a failed lookup.
    int resolveFidCommand(resource::GameResources& resources, const std::string& fidToken, std::ostream& out);

    /// `frm list <glob>`: emit JSON of the art entries whose name matches `glob` (e.g. ext2grd*),
    /// each {name, artPath, fid}. Scans the art LSTs. Returns 0 on success (an empty array is success).
    int listFrms(resource::GameResources& resources, const std::string& glob, std::ostream& out);

} // namespace cli
} // namespace geck
