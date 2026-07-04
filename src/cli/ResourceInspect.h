#pragma once

#include <iosfwd>
#include <string>

namespace geck {

namespace resource {
    class GameResources;
}

namespace cli {

    /// Resolve a single VFS path against the mounted data and report WHICH source provides it.
    /// Writes JSON: {path, found, source:{kind,path,label}|null}. "Not found" is a valid answer,
    /// not an error — the exit code is 0 either way. The direct answer to "is X present, and does it
    /// come from master.dat, a patch dir, or a mod?".
    int resourceFind(resource::GameResources& resources, const std::string& path, std::ostream& out);

    /// List every mounted entry whose path matches <glob> ('*' and '?'), each tagged with the source
    /// that provides it. Matching is case-insensitive and substring-anchored (tolerant of the VFS's
    /// leading slash / alias prefix), so "art/tiles/gras*" or "gras03*" both work. Writes JSON:
    /// {pattern, count, truncated, entries:[{path, source:{kind,path,label}}]} (capped; 'truncated'
    /// flags an over-long result). Browse a DAT / data set's contents without extracting.
    int resourceList(resource::GameResources& resources, const std::string& glob, std::ostream& out);

    /// Report the art a map REFERENCES but that does not resolve in the mounted data: the diagnostic
    /// for "why won't this map load / render fully". Writes JSON: {map, usedTileCount, objectArtCount,
    /// missingTiles:[{id,art}], missingObjectArt:[{pid,art}]}, where a missing entry carries an extra
    /// "reason" when the art path couldn't even be formed ("tiles.lst not mounted", "tile id out of
    /// tiles.lst range", "FID does not resolve"). Empty arrays mean everything resolves. Mirrors what
    /// the editor's (now tolerant) loader would skip. Exit code 1 only when the map itself can't be read.
    int resourceMissing(resource::GameResources& resources, const std::string& mapPath, std::ostream& out);

} // namespace cli
} // namespace geck
