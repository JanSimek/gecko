#include "cli/BundledResources.h"

#include "resource/GameResources.h"

#include <algorithm>
#include <system_error>
#include <vector>

namespace geck::cli {

std::filesystem::path findBundledResources(const std::string& argv0) {
    std::error_code ec;
    std::vector<std::filesystem::path> candidates;

    const std::filesystem::path exe(argv0);
    if (exe.has_parent_path()) {
        const std::filesystem::path dir = std::filesystem::absolute(exe, ec).parent_path();
        candidates.push_back(dir / "resources");               // alongside the binary (build/, install/)
        candidates.push_back(dir.parent_path() / "resources"); // one level up
        candidates.push_back(dir.parent_path() / "Resources" / "resources"); // gecko.app/Contents/MacOS
    }
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    candidates.push_back(cwd / "resources");
    candidates.push_back(cwd / "build" / "resources");

    // Confirm the dir is the real resources tree by a file we know lives there.
    const auto hit = std::ranges::find_if(candidates, [&ec](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate / "art" / "tiles" / "blank.frm", ec);
    });
    return hit != candidates.end() ? *hit : std::filesystem::path{};
}

void mountDataPaths(resource::GameResources& resources, const std::vector<std::string>& dataPaths,
    const std::string& argv0, const MountErrorHandler& onError) {
    const auto mount = [&resources, &onError](const std::filesystem::path& path) {
        if (!onError) {
            resources.files().addDataPath(path);
            return;
        }
        try {
            resources.files().addDataPath(path);
        } catch (const std::exception& e) {
            onError(path, e);
        }
    };
    for (const auto& path : dataPaths) {
        mount(path);
    }
    if (const auto bundled = findBundledResources(argv0); !bundled.empty()) {
        mount(bundled);
    }
}

} // namespace geck::cli
