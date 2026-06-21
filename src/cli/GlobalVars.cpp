#include "cli/GlobalVars.h"

#include "format/gam/Gam.h"
#include "resource/GameResources.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <ostream>

namespace geck::cli {

namespace {
    using ordered_json = nlohmann::ordered_json;
}

Gam* loadGameGam(resource::GameResources& resources) {
    for (const char* path : { "data/vault13.gam", "vault13.gam" }) {
        try {
            if (Gam* gam = resources.repository().load<Gam>(path); gam != nullptr) {
                return gam;
            }
        } catch (const std::exception&) {
            // try the next candidate path
        }
    }
    return nullptr;
}

int buildGlobalVars(resource::GameResources& resources, std::ostream& out) {
    Gam* gam = loadGameGam(resources);
    if (gam == nullptr || gam->gvarCount() == 0) {
        out << "{\"gvars\":[],\"stats\":{\"count\":0}}\n";
        return 1;
    }

    auto gvars = ordered_json::array();
    for (std::size_t i = 0; i < gam->gvarCount(); ++i) {
        gvars.push_back({ { "index", i }, { "name", gam->gvarKey(i) }, { "default", gam->gvarValue(i) } });
    }

    ordered_json root;
    root["gvars"] = std::move(gvars);
    root["stats"] = { { "count", gam->gvarCount() } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
