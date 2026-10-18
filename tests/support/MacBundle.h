#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "support/TempFile.h"

namespace geck::test {

/// The Info.plist of a fallout2-ce macOS bundle whose SDL_FILESYSTEM_BASE_DIR_TYPE is `baseDirType`
/// ("parent" in the shipped build), between the neighbouring keys a real one carries.
inline std::string bundlePlist(const std::string& baseDirType) {
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>fallout2-ce</string>
    <key>SDL_FILESYSTEM_BASE_DIR_TYPE</key>
    <string>)"
        + baseDirType + R"(</string>
    <key>CFBundleName</key>
    <string>Fallout II Community Edition</string>
</dict>
</plist>
)";
}

/// A minimal "Fallout II Community Edition.app" under `root`: its binary in Contents/MacOS and
/// `plist` as Contents/Info.plist, which is left out when `plist` is empty. Returns the bundle path.
inline std::filesystem::path makeAppBundle(const std::filesystem::path& root, std::string_view plist) {
    const std::filesystem::path bundle = root / "Fallout II Community Edition.app";
    writeFile(bundle / "Contents" / "MacOS" / "fallout2-ce", "binary");
    if (!plist.empty()) {
        writeFile(bundle / "Contents" / "Info.plist", plist);
    }
    return bundle;
}

} // namespace geck::test
