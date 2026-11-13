#pragma once

#include <memory>
#include <string>
#include <filesystem>
#include <vector>
#include <map>

#include "FileParser.h"
#include "ReaderExceptions.h"

namespace geck {

// Forward declarations
class Pro;
class Frm;
class Pal;
class Gam;
class Msg;
class Lst;

/**
 * @brief Picks the reader for a resource by its file extension and parses it.
 */
class ReaderFactory {
public:
    enum class Format {
        PRO,
        FRM,
        PAL,
        GAM,
        MSG,
        LST,
        UNKNOWN
    };

    /**
     * @brief Read file from memory data (for VFS support)
     * @tparam T The expected return type
     * @param data File data loaded from VFS
     * @param filename Original filename for error reporting and format detection
     * @return Created reader instance
     */
    template <typename T>
    [[nodiscard]] static std::unique_ptr<T> readFileFromMemory(const std::vector<uint8_t>& data, const std::string& filename) {
        try {
            auto reader = createReader<T>(detectByExtension(filename));
            if (!reader) {
                throw FileReaderException("Failed to create reader for " + filename);
            }
            return reader->openFile(filename, data);
        } catch (const std::exception& e) {
            throw IOException("ReaderFactory: Failed to read " + filename + " from memory: " + e.what(), filename);
        }
    }

    template <typename T>
    [[nodiscard]] static std::unique_ptr<FileParser<T>> createReader(Format format);

    [[nodiscard]] static Format detectByExtension(const std::filesystem::path& filePath);

private:
    static const std::map<std::string, Format> extension_format_map;
};

// Template specializations (declared here, implemented in .cpp)
template <>
std::unique_ptr<FileParser<Pro>> ReaderFactory::createReader<Pro>(Format format);
template <>
std::unique_ptr<FileParser<Frm>> ReaderFactory::createReader<Frm>(Format format);
template <>
std::unique_ptr<FileParser<Pal>> ReaderFactory::createReader<Pal>(Format format);
template <>
std::unique_ptr<FileParser<Gam>> ReaderFactory::createReader<Gam>(Format format);
template <>
std::unique_ptr<FileParser<Msg>> ReaderFactory::createReader<Msg>(Format format);
template <>
std::unique_ptr<FileParser<Lst>> ReaderFactory::createReader<Lst>(Format format);
// Note: Map reader is not supported via createReader due to callback requirement
// Use MapReader constructor directly with the required callback

// Template implementations for generic createReader
template <typename T>
inline std::unique_ptr<FileParser<T>> ReaderFactory::createReader(Format format) {
    (void)format;
    static_assert(sizeof(T) == 0, "Unsupported type for ReaderFactory::createReader. Add explicit specialization.");
    return nullptr;
}

} // namespace geck
