#pragma once

#include "../util/Factory.h"
#include "FileParser.h"
#include "format/IFile.h"
#include <filesystem>
#include <any>

namespace geck {

// Forward declarations
class Dat;
class Pro; 
class Frm;
class Pal;
class Gam;
class Msg;
class Lst;
class Map;

/**
 * @brief Arguments for file reader creation
 */
struct ReaderArgs {
    std::filesystem::path filepath;
    std::shared_ptr<std::istream> stream;
};

/**
 * @brief Unified reader factory using the generic Factory template
 * 
 * Replaces the old ReaderFactory with a cleaner implementation
 * that follows DRY principles.
 */
class UnifiedReaderFactory : public SingletonFactory<IFile> {
public:
    /**
     * @brief Initialize the factory with all supported readers
     */
    static void initialize() {
        auto& factory = getInstance();
        
        // Register DAT reader
        factory.registerTypeWithArgs("dat", [](const std::any& args) -> std::unique_ptr<IFile> {
            auto readerArgs = std::any_cast<ReaderArgs>(args);
            return createDatReader(readerArgs.filepath, readerArgs.stream);
        });
        
        // Register PRO reader
        factory.registerTypeWithArgs("pro", [](const std::any& args) -> std::unique_ptr<IFile> {
            auto readerArgs = std::any_cast<ReaderArgs>(args);
            return createProReader(readerArgs.filepath, readerArgs.stream);
        });
        
        // Register FRM reader
        factory.registerTypeWithArgs("frm", [](const std::any& args) -> std::unique_ptr<IFile> {
            auto readerArgs = std::any_cast<ReaderArgs>(args);
            return createFrmReader(readerArgs.filepath, readerArgs.stream);
        });
        
        // Register other readers...
        factory.registerTypeWithArgs("pal", [](const std::any& args) -> std::unique_ptr<IFile> {
            auto readerArgs = std::any_cast<ReaderArgs>(args);
            return createPalReader(readerArgs.filepath, readerArgs.stream);
        });
        
        factory.registerTypeWithArgs("gam", [](const std::any& args) -> std::unique_ptr<IFile> {
            auto readerArgs = std::any_cast<ReaderArgs>(args);
            return createGamReader(readerArgs.filepath, readerArgs.stream);
        });
        
        factory.registerTypeWithArgs("msg", [](const std::any& args) -> std::unique_ptr<IFile> {
            auto readerArgs = std::any_cast<ReaderArgs>(args);
            return createMsgReader(readerArgs.filepath, readerArgs.stream);
        });
        
        factory.registerTypeWithArgs("lst", [](const std::any& args) -> std::unique_ptr<IFile> {
            auto readerArgs = std::any_cast<ReaderArgs>(args);
            return createLstReader(readerArgs.filepath, readerArgs.stream);
        });
        
        factory.registerTypeWithArgs("map", [](const std::any& args) -> std::unique_ptr<IFile> {
            auto readerArgs = std::any_cast<ReaderArgs>(args);
            return createMapReader(readerArgs.filepath, readerArgs.stream);
        });
    }
    
    /**
     * @brief Create a reader for the given file type
     * @param type File type (extension without dot)
     * @param filepath Path to file
     * @param stream Input stream
     * @return Created reader instance
     */
    static std::unique_ptr<IFile> createReader(
        const std::string& type,
        const std::filesystem::path& filepath,
        std::shared_ptr<std::istream> stream) {
        
        ReaderArgs args{filepath, stream};
        return getInstance().createWithArgs(type, args);
    }
    
    /**
     * @brief Auto-detect file type and create appropriate reader
     * @param filepath Path to file
     * @return Created reader instance
     */
    static std::unique_ptr<IFile> createReaderAutoDetect(
        const std::filesystem::path& filepath) {
        
        auto extension = filepath.extension().string();
        if (!extension.empty() && extension[0] == '.') {
            extension = extension.substr(1);
        }
        
        // Convert to lowercase
        std::ranges::transform(extension, extension.begin(), ::tolower);
        
        auto stream = std::make_shared<std::ifstream>(filepath, std::ios::binary);
        if (!stream->is_open()) {
            throw IOException("Cannot open file", filepath);
        }
        
        return createReader(extension, filepath, stream);
    }

private:
    // Static factory methods for each reader type
    static std::unique_ptr<Dat> createDatReader(
        const std::filesystem::path& filepath,
        std::shared_ptr<std::istream> stream);
    
    static std::unique_ptr<Pro> createProReader(
        const std::filesystem::path& filepath,
        std::shared_ptr<std::istream> stream);
    
    static std::unique_ptr<Frm> createFrmReader(
        const std::filesystem::path& filepath,
        std::shared_ptr<std::istream> stream);
    
    static std::unique_ptr<Pal> createPalReader(
        const std::filesystem::path& filepath,
        std::shared_ptr<std::istream> stream);
    
    static std::unique_ptr<Gam> createGamReader(
        const std::filesystem::path& filepath,
        std::shared_ptr<std::istream> stream);
    
    static std::unique_ptr<Msg> createMsgReader(
        const std::filesystem::path& filepath,
        std::shared_ptr<std::istream> stream);
    
    static std::unique_ptr<Lst> createLstReader(
        const std::filesystem::path& filepath,
        std::shared_ptr<std::istream> stream);
    
    static std::unique_ptr<Map> createMapReader(
        const std::filesystem::path& filepath,
        std::shared_ptr<std::istream> stream);
};

} // namespace geck