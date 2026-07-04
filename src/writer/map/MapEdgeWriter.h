#pragma once

#include "format/map/MapEdge.h"
#include "writer/FileWriter.h"

namespace geck {

/// Serializes a MapEdge to a Fallout 2 CE / sfall ".EDG" map-edge sidecar.
///
/// Follows fallout2-ce `writeEdgStream` (`map_edge.cc`): a big-endian `EDGE` header, then per
/// elevation an optional v2 square/clip block followed by each zone rect trailed by a "level
/// indicator" (the current elevation while more zones follow, otherwise the next elevation that
/// has zones — so the reader advances past empty elevations).
///
/// One deliberate divergence from `writeEdgStream`, matching every shipped `.edg`: the file's
/// final zone omits its trailing indicator when it lies on the last elevation (the original
/// mapper writes N zones with N-1 separators; the CE writer appends a redundant terminator the
/// reader tolerates but no real file carries). This makes reading an engine-produced file and
/// writing it back reproduce the original bytes exactly.
///
/// Callers must skip writing an edge with no zones (`MapEdge::empty()`): the engine writes
/// no file in that case and a zero-zone file does not parse.
class MapEdgeWriter : public FileWriter<MapEdge> {
public:
    bool write(const MapEdge& edge) override;
};

} // namespace geck
