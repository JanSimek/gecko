#pragma once

#include <cstddef>
#include <string>

namespace geck::mcp {

/// RFC 4648 base64 (with '=' padding). Used to embed binary tool output — a rendered image — as
/// an MCP image content block, which carries its bytes base64-encoded in JSON.
std::string encodeBase64(const unsigned char* data, std::size_t size);

} // namespace geck::mcp
