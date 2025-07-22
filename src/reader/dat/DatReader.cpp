#include "DatReader.h"

#include <stdexcept>
#include <spdlog/spdlog.h>

#include "format/dat/Dat.h"
#include "format/dat/DatEntry.h"
#include "format/IFile.h"

namespace geck {

std::unique_ptr<Dat> DatReader::read() {
    try {
        spdlog::debug("Reading DAT file: {}", _path.string());

        _stream.setPosition(_stream.size() - FOOTER_SIZE);

        uint32_t tree_size = read_le_u32();
        uint32_t data_size = read_le_u32();

        if (data_size != _stream.size()) {
            throw ParseException("Stored file size and real size don't match", _path);
        }

        // tree_size includes file_count field size
        uint32_t file_count_offset = data_size - FOOTER_SIZE - tree_size;
        _stream.setPosition(file_count_offset);

        uint32_t file_count = read_le_u32();
        spdlog::debug("Reading {} DAT entries", file_count);

        auto dat = std::make_unique<Dat>();

        // Read DAT entry information
        for (size_t i = 0; i < file_count; ++i) {
            std::unique_ptr<DatEntry> entry = std::make_unique<DatEntry>();

            uint32_t filename_size = read_le_u32();
            if (filename_size == 0 || filename_size > 1024) {
                throw ParseException("Invalid filename size: " + std::to_string(filename_size), _path);
            }

            std::string filename = read_str(filename_size);
            // normalize file path
            std::replace(filename.begin(), filename.end(), '\\', '/');
            std::transform(filename.begin(), filename.end(), filename.begin(),
                [](unsigned char c) { return std::tolower(c); });
            entry->setFilename(filename);

            bool compressed = static_cast<uint8_t>(read_u8());
            entry->setCompressed(compressed);

            uint32_t unpacked_size = read_le_u32();
            entry->setDecompressedSize(unpacked_size);

            uint32_t packed_size = read_le_u32();
            entry->setPackedSize(packed_size);

            uint32_t data_offset = read_le_u32();
            entry->setOffset(data_offset);

            dat->addEntry(filename, std::move(entry));
        }

        spdlog::debug("Successfully read DAT file with {} entries", file_count);
        return dat;

    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException("Failed to parse DAT file: " + std::string(e.what()), _path);
    }
}

} // namespace geck
