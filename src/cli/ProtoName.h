#pragma once

#include <cstdint>
#include <string>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// A proto's display name from its type's pro_*.msg (what proto_info shows), or "" when the proto or
/// its message can't be loaded.
std::string protoDisplayName(resource::GameResources& resources, uint32_t pid);

} // namespace geck::cli
