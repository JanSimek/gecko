#include "resource/MapNameEditor.h"

#include "format/maps/MapsTxt.h"
#include "format/msg/Msg.h"
#include "reader/maps/MapsTxtReader.h"
#include "reader/msg/MsgReader.h"
#include "resource/WritableDataRoot.h"
#include "util/FileIo.h"
#include "writer/maps/MapsTxtSerializer.h"
#include "writer/maps/MapsTxtValidator.h"
#include "writer/msg/MsgSerializer.h"

#include <utility>
#include <vector>

namespace geck::resource {

namespace {

    using geck::io::readFile;
    using geck::io::writeFile;

    constexpr const char* kMapsTxt = "data/maps.txt";
    constexpr const char* kMapMsg = "text/english/game/map.msg";

    std::string formatErrors(const std::vector<writer::MapsTxtIssue>& issues) {
        std::string text = "Refusing to save: maps.txt would be invalid.\n";
        for (const writer::MapsTxtIssue& issue : issues) {
            if (issue.severity == writer::MapsTxtIssue::Severity::Error) {
                text += "\n  [Map " + std::to_string(issue.section) + "] " + issue.message;
            }
        }
        return text;
    }

} // namespace

MapNameEditResult saveMapNames(DataFileSystem& files, const std::filesystem::path& writableRoot,
    int mapIndex, int elevation,
    const std::optional<std::string>& lookupName, const std::optional<std::string>& displayName) {
    // Prepare (and validate) every edit before writing anything, so a rejected maps.txt leaves both
    // files untouched.
    std::vector<std::pair<std::filesystem::path, std::string>> pending;

    if (lookupName.has_value()) {
        const std::filesystem::path path = ensureWritableCopy(files, writableRoot, kMapsTxt);
        MapsTxt doc = parseMapsTxt(readFile(path));
        if (!writer::setField(doc, mapIndex, "lookup_name", *lookupName)) {
            return { false, "Could not find this map's lookup_name in maps.txt." };
        }
        if (const auto issues = writer::validateMapsTxt(doc); writer::hasErrors(issues)) {
            return { false, formatErrors(issues) }; // hard block — a broken registry would corrupt the game
        }
        pending.emplace_back(path, writer::serializeMapsTxt(doc));
    }

    if (displayName.has_value()) {
        const std::filesystem::path path = ensureWritableCopy(files, writableRoot, kMapMsg);
        Msg msg = parseMsg(path, readFile(path));
        msg.setMessageText(mapIndex * 3 + elevation + 200, *displayName);
        pending.emplace_back(path, writer::serializeMsg(msg));
    }

    for (const auto& [path, content] : pending) {
        writeFile(path, content);
    }
    return { true, {} };
}

} // namespace geck::resource
