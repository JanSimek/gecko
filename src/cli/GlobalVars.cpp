#include "cli/GlobalVars.h"

#include "cli/GvarRoles.h"
#include "resource/GameVarsLoader.h"

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
    return resource::loadVault13Gam(resources);
}

int buildGlobalVars(resource::GameResources& resources, std::ostream& out) {
    Gam* gam = loadGameGam(resources);
    if (gam == nullptr || gam->gvarCount() == 0) {
        out << "{\"gvars\":[],\"stats\":{\"count\":0}}\n";
        return 1;
    }

    const auto roles = buildGvarRoles(resources);
    auto gvars = ordered_json::array();
    for (std::size_t i = 0; i < gam->gvarCount(); ++i) {
        ordered_json gvar = { { "index", i }, { "name", gam->gvarKey(i) }, { "default", gam->gvarValue(i) } };
        if (const auto it = roles.find(static_cast<int>(i)); it != roles.end()) {
            gvar["roles"] = it->second;
        }
        gvars.push_back(std::move(gvar));
    }

    ordered_json root;
    root["gvars"] = std::move(gvars);
    root["stats"] = { { "count", gam->gvarCount() } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
