#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "support/ByteWriter.h"

namespace geck::test {

/// A compiled script (.int) with one procedure and no static strings, laid out as fallout2-ce's
/// programCreateByPath reads it: 42 header bytes, the procedure table, the identifier table (names
/// addressed from its length field), the static-string table's length, then `code`.
inline std::vector<uint8_t> compiledScript(const std::string& procedure, const std::vector<uint8_t>& code) {
    const auto names = static_cast<uint32_t>(procedure.size() + 1);
    const uint32_t body = 42 + 4 + 24 + 4 + names + 4;
    ByteWriter bytes;
    bytes.fill(0, 42).be32(1);                                // procedure count
    bytes.be32(4).be32(0).be32(0).be32(0).be32(body).be32(0); // name, flags, time, condition, body, args
    bytes.be32(names);
    for (const char c : procedure) {
        bytes.u8(static_cast<uint8_t>(c));
    }
    bytes.u8(0).be32(0xFFFFFFFF); // no static strings
    std::vector<uint8_t> out = bytes.data();
    out.insert(out.end(), code.begin(), code.end());
    return out;
}

/// `push <value>` (an integer) in a compiled script's code.
inline ByteWriter& pushInt(ByteWriter& code, int32_t value) {
    return code.be16(0xC001).be32(static_cast<uint32_t>(value));
}

} // namespace geck::test
