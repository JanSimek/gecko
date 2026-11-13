#pragma once

#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// Locate the bundled `resources` data directory (blank.frm, scripts, …) that ships next to the
/// binary, searching relative to the executable (`argv0`) and then the working directory. Returns an
/// empty path if not found. The headless tools mount this so they resolve the same fallback assets
/// the editor does — e.g. art/tiles/blank.frm, which the natural map render needs but isn't in
/// master.dat.
std::filesystem::path findBundledResources(const std::string& argv0);

/// Called with a path that failed to mount and why; the remaining paths still mount.
using MountErrorHandler = std::function<void(const std::filesystem::path&, const std::exception&)>;

/// Mount `dataPaths` in order, then the bundled resources (see findBundledResources). Without an
/// `onError` handler a mount failure propagates.
void mountDataPaths(resource::GameResources& resources, const std::vector<std::string>& dataPaths,
    const std::string& argv0, const MountErrorHandler& onError = {});

} // namespace geck::cli
