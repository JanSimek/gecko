#pragma once

#include <filesystem>
#include <string>

namespace geck::cli {

/// Locate the bundled `resources` data directory (blank.frm, scripts, …) that ships next to the
/// binary, searching relative to the executable (`argv0`) and then the working directory. Returns an
/// empty path if not found. The headless tools mount this so they resolve the same fallback assets
/// the editor does — e.g. art/tiles/blank.frm, which the natural map render needs but isn't in
/// master.dat.
std::filesystem::path findBundledResources(const std::string& argv0);

} // namespace geck::cli
