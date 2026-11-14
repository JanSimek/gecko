#pragma once

#include <QString>

namespace geck::pattern {

/// Locates the user's pattern library on disk. Patterns live alongside the app's
/// settings, under <ConfigLocation>/gecko/patterns, and may be organised into
/// subfolders. The browser and the save/load dialogs default to this location.
class PatternLibrary {
public:
    /// The pattern library root, created on first use. Returns an absolute path.
    static QString rootDir();
    /// <root>/Custom, created on first use: where the user's own saved patterns go. The browser
    /// lists folders only, so a pattern saved loose in the root would never be shown.
    static QString customDir();
};

} // namespace geck::pattern
