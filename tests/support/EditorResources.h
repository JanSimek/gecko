#pragma once

#include <filesystem>
#include <memory>

#include "resource/GameResources.h"
#include "support/Fixtures.h"

#ifndef GECK_RESOURCES_DIR
#error "GECK_RESOURCES_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

namespace geck::test {

/// GameResources holding the editor's bundled art (the repo's resources/, e.g. art/misc/HEX.frm)
/// plus the fixture colour palette — what an EditorWidget needs to build its canvas without game
/// data.
inline std::shared_ptr<resource::GameResources> editorResources() {
    auto resources = std::make_shared<resource::GameResources>();
    resources->files().addDataPath(std::filesystem::path(GECK_RESOURCES_DIR));
    resources->files().addDataPath(dataDir()); // color.pal
    return resources;
}

} // namespace geck::test
