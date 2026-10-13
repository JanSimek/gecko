#include "cli/GvarRefs.h"

#include "cli/ScriptRefs.h"

#include <ostream>

namespace geck::cli {

int findGvarRefs(resource::GameResources& resources, const std::string& gvarName, std::ostream& out) {
    if (gvarName.empty()) {
        out << "{\"gvar\":\"\",\"refs\":[],\"stats\":{\"refs\":0,\"setters\":0,\"scripts\":0}}\n";
        return 1; // an empty name would match every access
    }
    return findScriptRefs(resources, { "gvar", gvarName }, out);
}

} // namespace geck::cli
