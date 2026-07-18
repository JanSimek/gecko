#include "mcp/Base64.h"

namespace geck::mcp {

std::string encodeBase64(const unsigned char* data, std::size_t size) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const unsigned int n = (static_cast<unsigned int>(data[i]) << 16)
            | (static_cast<unsigned int>(data[i + 1]) << 8) | data[i + 2];
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kAlphabet[(n >> 6) & 0x3F]);
        out.push_back(kAlphabet[n & 0x3F]);
    }
    if (const std::size_t rest = size - i; rest == 1) {
        const unsigned int n = static_cast<unsigned int>(data[i]) << 16;
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.append("==");
    } else if (rest == 2) {
        const unsigned int n = (static_cast<unsigned int>(data[i]) << 16)
            | (static_cast<unsigned int>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kAlphabet[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

} // namespace geck::mcp
