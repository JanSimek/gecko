#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace geck::test {

/// Builds an in-memory binary buffer for reader fixtures. Big-endian helpers
/// match the engine's on-disk byte order (FRM/MAP/PRO are big-endian).
class ByteWriter {
public:
    ByteWriter& u8(uint8_t v) {
        _bytes.push_back(v);
        return *this;
    }

    ByteWriter& be16(uint16_t v) {
        _bytes.push_back(static_cast<uint8_t>(v >> 8));
        _bytes.push_back(static_cast<uint8_t>(v));
        return *this;
    }

    ByteWriter& be32(uint32_t v) {
        _bytes.push_back(static_cast<uint8_t>(v >> 24));
        _bytes.push_back(static_cast<uint8_t>(v >> 16));
        _bytes.push_back(static_cast<uint8_t>(v >> 8));
        _bytes.push_back(static_cast<uint8_t>(v));
        return *this;
    }

    /// Appends `text` and its NUL terminator.
    ByteWriter& cstring(std::string_view text) {
        _bytes.insert(_bytes.end(), text.begin(), text.end());
        _bytes.push_back(0);
        return *this;
    }

    /// Overwrites the four bytes at `at` with big-endian `v`, for an offset known only once the
    /// bytes it points at are written.
    ByteWriter& patchBe32(size_t at, uint32_t v) {
        _bytes.at(at + 3) = static_cast<uint8_t>(v);
        _bytes[at + 2] = static_cast<uint8_t>(v >> 8);
        _bytes[at + 1] = static_cast<uint8_t>(v >> 16);
        _bytes[at] = static_cast<uint8_t>(v >> 24);
        return *this;
    }

    /// Appends `count` copies of `value`.
    ByteWriter& fill(uint8_t value, size_t count) {
        _bytes.insert(_bytes.end(), count, value);
        return *this;
    }

    const std::vector<uint8_t>& data() const { return _bytes; }
    size_t size() const { return _bytes.size(); }

private:
    std::vector<uint8_t> _bytes;
};

} // namespace geck::test
