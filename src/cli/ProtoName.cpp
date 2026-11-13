#include "cli/ProtoName.h"

#include "format/msg/Msg.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include <spdlog/spdlog.h>

#include <exception>

namespace geck::cli {

std::string protoDisplayName(resource::GameResources& resources, uint32_t pid) {
    try {
        if (const Pro* pro = resources.tryLoadPro(pid); pro != nullptr) {
            if (Msg* msg = ProHelper::msgFile(resources, pro->type()); msg != nullptr) {
                return msg->message(pro->header.message_id).text;
            }
        }
    } catch (const std::exception& e) {
        // An unresolvable proto leaves the name empty rather than inventing one.
        spdlog::debug("no display name for pid {:#x}: {}", pid, e.what());
    }
    return {};
}

} // namespace geck::cli
