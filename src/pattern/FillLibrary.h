#pragma once

#include <QString>

namespace geck::pattern {

/// Locates the area-fill recipe library. Mirrors PatternLibrary: user fills live under
/// <ConfigLocation>/gecko/fills (where "save fill" writes), and a read-only set of bundled examples
/// ships with the editor under <resources>/scripts/fills. The Fill dialog shows both (a user fill of
/// the same file name wins), exactly as the script console merges bundled + library stamps.
class FillLibrary {
public:
    /// The user fill library root, created on first use. Absolute path.
    static QString rootDir();
    /// The bundled (read-only) example-fills directory shipped with the editor. Absolute path; may
    /// not exist in a dev tree without the resources folder populated.
    static QString bundledDir();
};

} // namespace geck::pattern
