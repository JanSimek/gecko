#include "DatReader.h"

#include <stdexcept>
#include <spdlog/spdlog.h>

#include "format/dat/Dat.h"
#include "format/dat/DatEntry.h"
#include "format/IFile.h"
#include "../ErrorMessages.h"

namespace geck {

std::unique_ptr<Dat> DatReader::read() {
    try {
        // Use format validation
        FormatValidator::validateDatFile(getBinaryUtils(), _path);

        auto& utils = getBinaryUtils();
        spdlog::debug("Reading DAT file: {}", _path.string());

        utils.setPosition(utils.getPosition().total - FOOTER_SIZE);

        uint32_t tree_size = utils.readBE32();
        uint32_t data_size = utils.readBE32();

        if (data_size != utils.getPosition().total) {
            throw ParseException("Stored file size and real size don't match", _path);
        }

        // tree_size includes file_count field size
        uint32_t file_count_offset = data_size - FOOTER_SIZE - tree_size;
        utils.setPosition(file_count_offset);

        uint32_t file_count = utils.readBE32();
        spdlog::debug("Reading {} DAT entries", file_count);

        auto dat = std::make_unique<Dat>();

        // Read DAT entry information using BinaryUtils
        for (size_t i = 0; i < file_count; ++i) {
            std::unique_ptr<DatEntry> entry = std::make_unique<DatEntry>();

            uint32_t filename_size = utils.readBE32();
            if (filename_size == 0 || filename_size > 1024) {
                throw ParseException(
                    ErrorMessages::invalidStringLength(_path, filename_size), _path);
            }

            std::string filename = utils.readFixedString(filename_size);
            // normalize file path
            std::replace(filename.begin(), filename.end(), '\\', '/');
            std::transform(filename.begin(), filename.end(), filename.begin(),
                [](unsigned char c) { return std::tolower(c); });
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
