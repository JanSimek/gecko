#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>

#include "format/map/Map.h"

namespace geck {

namespace resource {
    class GameResources;
}

/**
 * @brief Writes a map to @p path with the standard PID→Pro resolver. Qt-free.
 *
 * The shared core of EditorWidget::saveMap and GameLauncher's "save into the game data dir"
 * step, which previously each built an identical MapWriter + proto-resolver inline. The writer
 * needs a proto provider because scenery/item objects read their subtype from the proto during
 * serialization (the same provider the reader uses).
 *
 * @return the byte count on success, or std::nullopt if MapWriter::write reported failure
 *         (the non-exception "write returned false" path the callers already handle).
 *         Propagates FileWriterException (and other load/IO exceptions) thrown while opening
 *         the file or resolving a proto, so callers can surface them.
 */
std::optional<std::size_t> saveMapToFile(resource::GameResources& resources,
    const Map::MapFile& mapFile,
    const std::filesystem::path& path);

} // namespace geck
