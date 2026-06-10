#pragma once

#include <cstddef>
#include <cstdint>
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
