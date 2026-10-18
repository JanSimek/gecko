#include "DatReader.h"

#include <stdexcept>
#include <spdlog/spdlog.h>

#include "format/dat/Dat.h"
#include "format/dat/DatEntry.h"
#include "format/IFile.h"
#include "reader/ErrorMessages.h"
#include "reader/TextParsing.h"
#include <algorithm>

namespace geck {

std::unique_ptr<Dat> DatReader::read() {
    try {
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
