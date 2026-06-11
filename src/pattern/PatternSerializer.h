#pragma once

#include <optional>

#include <QByteArray>
#include <QString>

#include "pattern/Pattern.h"

namespace geck::pattern {

/// JSON (de)serialization for the Tier-1 prefab format (PLAN.md §4). PID, direction
/// and tile-id values are preserved verbatim; the format stores no display labels.
///
/// Serialization is total. Deserialization is validated: it rejects malformed JSON,
/// a missing/unsupported `version`, and entries missing required fields, returning
/// std::nullopt and (optionally) a human-readable reason rather than a partial
/// pattern — there is no silent fallback (engine-data-fidelity rule).
class PatternSerializer {
public:
    /// Serializes a pattern to pretty-printed UTF-8 JSON bytes.
    static QByteArray serialize(const Pattern& pattern);

    /// Parses JSON bytes into a Pattern. On failure returns std::nullopt and, if
    /// `error` is non-null, sets it to a description of the first problem found.
    static std::optional<Pattern> deserialize(const QByteArray& data, QString* error = nullptr);
};

} // namespace geck::pattern
