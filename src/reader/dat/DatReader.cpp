#include "DatReader.h"

#include <stdexcept>
#include <spdlog/spdlog.h>

#include "format/dat/Dat.h"
#include "format/dat/DatEntry.h"
#include "format/IFile.h"
#include "reader/ErrorMessages.h"
#include "reader/TextParsing.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace geck {

// DAT2 ends in a footer whose last word is the archive's own size; DAT1 has
// no footer, so that word is anything else.
bool DatReader::isDat2() {
    auto& utils = getBinaryUtils();
    const size_t total = utils.getPosition().total;
    if (total < 8) {
        return false;
    }
    utils.setPosition(total - 4);
    const uint32_t dataSize = utils.readBE32(); // the stream is little-endian for DAT2
    utils.setPosition(0);
    return dataSize == total;
}

// Fallout 1's DAT1, all big-endian: u32 dir count + 3 words; the directory
// names (u8 length + name, "." for the root); then per directory u32 file
// count + 3 words, and per file u8 length + name, u32 flags (0x40 = LZSS),
// offset, size, packed size.
std::unique_ptr<Dat> DatReader::readDat1() {
    const size_t total = getBinaryUtils().getPosition().total;
    auto be32 = [this]() {
        uint8_t b[4];
        read_bytes(b, 4);
        return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
    };
    auto name = [this]() {
        uint8_t len = 0;
        read_bytes(&len, 1);
        std::string s(len, '\0');
        if (len) {
            read_bytes(reinterpret_cast<uint8_t*>(s.data()), len);
        }
        return s;
    };

    setPosition(0);
    const uint32_t dirCount = be32();
    if (dirCount == 0 || dirCount > 10000) {
        throw UnsupportedFormatException(ErrorMessages::datFileError(_path, "neither a DAT2 nor a DAT1 archive"), _path);
    }
    be32();
    be32();
    be32();
    std::vector<std::string> dirs;
    dirs.reserve(dirCount);
    for (uint32_t i = 0; i < dirCount; ++i) {
        dirs.push_back(name());
    }

    auto dat = std::make_unique<Dat>();
    size_t files = 0;
    for (const auto& dir : dirs) {
        const uint32_t count = be32();
        be32();
        be32();
        be32();
        for (uint32_t i = 0; i < count; ++i) {
            std::string filename = name();
            const uint32_t flags = be32();
            const uint32_t offset = be32();
            const uint32_t size = be32();
            const uint32_t packed = be32();

            std::string full = (dir == "." ? filename : dir + "/" + filename);
            std::replace(full.begin(), full.end(), '\\', '/');
            std::transform(full.begin(), full.end(), full.begin(), [](unsigned char c) { return std::tolower(c); });

            const bool lzss = (flags & 0x40) != 0;
            if (offset > total || (lzss ? packed : size) > total - offset) {
                throw ParseException(ErrorMessages::datFileError(_path, "entry " + full + " runs past the end of the archive"), _path);
            }
            auto entry = std::make_unique<DatEntry>();
            entry->setFilename(full);
            entry->setCompressed(lzss);
            entry->setLzss(lzss);
            entry->setDecompressedSize(size);
            entry->setPackedSize(lzss ? packed : size);
            entry->setOffset(offset);
            dat->addEntry(full, std::move(entry));
            ++files;
        }
    }
    spdlog::debug("Read DAT1 file with {} entries", files);
    return dat;
}

void DatReader::unpackLzss(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    size_t p = 0;
    size_t out = 0;
    while (p + 2 <= srcSize && out < dstSize) {
        const uint16_t v = static_cast<uint16_t>((src[p] << 8) | src[p + 1]);
        p += 2;
        if (v == 0) {
            break;
        }
        if (v & 0x8000) {
            size_t len = v & 0x7FFF;
            len = std::min({ len, srcSize - p, dstSize - out });
            std::memcpy(dst + out, src + p, len);
            out += len;
            p += v & 0x7FFF;
            continue;
        }
        const size_t end = std::min(srcSize, p + v);
        uint8_t ring[4096];
        std::memset(ring, ' ', sizeof(ring));
        size_t r = 4078;
        while (p < end && out < dstSize) {
            const uint8_t flags = src[p++];
            for (int bit = 0; bit < 8 && p < end && out < dstSize; ++bit) {
                if (flags & (1 << bit)) {
                    const uint8_t c = src[p++];
                    dst[out++] = c;
                    ring[r] = c;
                    r = (r + 1) & 0xFFF;
                } else {
                    if (p + 1 >= end) {
                        p = end;
                        break;
                    }
                    const uint8_t b1 = src[p];
                    const uint8_t b2 = src[p + 1];
                    p += 2;
                    const size_t pos = b1 | ((b2 & 0xF0) << 4);
                    const size_t len = (b2 & 0x0F) + 3;
                    for (size_t k = 0; k < len && out < dstSize; ++k) {
                        const uint8_t c = ring[(pos + k) & 0xFFF];
                        dst[out++] = c;
                        ring[r] = c;
                        r = (r + 1) & 0xFFF;
                    }
                }
            }
        }
        p = end;
    }
    if (out != dstSize) {
        throw ParseException("LZSS entry unpacked to " + std::to_string(out) + " of " + std::to_string(dstSize) + " bytes");
    }
}

std::unique_ptr<Dat> DatReader::read() {
    try {
        if (!isDat2()) {
            return readDat1();
        }
        auto& utils = getBinaryUtils();
        spdlog::debug("Reading DAT file: {}", _path.string());

        // A DAT2 archive's footer (its last 8 bytes) holds the directory tree size and the archive size.
        const size_t fileSize = utils.getPosition().total;
        if (fileSize < FOOTER_SIZE) {
            throw UnsupportedFormatException(ErrorMessages::invalidFileSize(_path, fileSize, FOOTER_SIZE), _path);
        }
        utils.setPosition(fileSize - FOOTER_SIZE);

        uint32_t tree_size = utils.readBE32();
        uint32_t data_size = utils.readBE32();

        if (data_size != fileSize) {
            throw UnsupportedFormatException(
                ErrorMessages::datFileError(_path,
                    "File size mismatch: header says " + std::to_string(data_size) + " bytes, actual file is " + std::to_string(fileSize) + " bytes"),
                _path);
        }
        if (tree_size == 0 || tree_size > fileSize) {
            throw UnsupportedFormatException(
                ErrorMessages::datFileError(_path, "Invalid tree size: " + std::to_string(tree_size)), _path);
        }

        // tree_size includes file_count field size
        uint32_t file_count_offset = data_size - FOOTER_SIZE - tree_size;
        utils.setPosition(file_count_offset);

        uint32_t file_count = utils.readBE32();
        spdlog::debug("Reading {} DAT entries", file_count);

        auto dat = std::make_unique<Dat>();

        for (size_t i = 0; i < file_count; ++i) {
            std::unique_ptr<DatEntry> entry = std::make_unique<DatEntry>();

            uint32_t filename_size = utils.readBE32();
            if (filename_size == 0 || filename_size > 1024) {
                throw ParseException(
                    ErrorMessages::invalidStringLength(_path, filename_size), _path);
            }

            std::string filename = text::toLower(utils.readFixedString(filename_size));
            // normalize file path
            std::replace(filename.begin(), filename.end(), '\\', '/');
            entry->setFilename(filename);

            bool compressed = static_cast<bool>(utils.readU8());
            entry->setCompressed(compressed);

            uint32_t unpacked_size = utils.readBE32();
            entry->setDecompressedSize(unpacked_size);

            uint32_t packed_size = utils.readBE32();
            entry->setPackedSize(packed_size);

            uint32_t data_offset = utils.readBE32();
            entry->setOffset(data_offset);

            dat->addEntry(filename, std::move(entry));

            if (i % 1000 == 0) {
                auto pos = utils.getPosition();
                spdlog::trace("Read {} entries ({:.1f}% complete)", i, pos.percentage());
            }
        }

        spdlog::debug("Successfully read DAT file with {} entries", file_count);
        return dat;

    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException(
            ErrorMessages::datFileError(_path, "Parse failed: " + std::string(e.what())), _path);
    }
}

} // namespace geck
