#pragma once

#include <chrono>
#include <string>
#include <filesystem>
#include <map>
#include <vector>
#include <spdlog/spdlog.h>

namespace geck {

struct ParseStatistics {
    std::chrono::microseconds parseTime{ 0 };
    size_t fileSize = 0;
    size_t bytesRead = 0;
    size_t fieldsSkipped = 0;
    size_t validationsPassed = 0;
    size_t warningsGenerated = 0;
    std::string formatType;

    double throughputMBps() const {
        if (parseTime.count() == 0)
            return 0.0;
        return (static_cast<double>(fileSize) / (1024.0 * 1024.0)) / (static_cast<double>(parseTime.count()) / 1000000.0);
    }

    double efficiency() const {
        if (fileSize == 0)
            return 0.0;
        return static_cast<double>(bytesRead) / fileSize * 100.0;
    }
};

class ReaderDiagnostics {

public:
    // RAII timer for automatic timing
    class ParseTimer {
        std::chrono::high_resolution_clock::time_point startTime;

    public:
        ParseTimer()
            : startTime(std::chrono::high_resolution_clock::now()) { }

        std::chrono::microseconds getElapsed() {
            auto endTime = std::chrono::high_resolution_clock::now();
            return std::chrono::duration_cast<std::chrono::microseconds>(
                endTime - startTime);
        }
    };

    // Simple logging-based diagnostics
    static void logParseStart(const std::filesystem::path& filePath, const std::string& formatType) {
        size_t fileSize = std::filesystem::file_size(filePath);
        spdlog::trace("Starting parse: {} ({} bytes, {} format)",
            filePath.string(), fileSize, formatType);
    }

    static void logParseComplete(const std::filesystem::path& filePath,
        std::chrono::microseconds parseTime) {
        size_t fileSize = std::filesystem::file_size(filePath);
        double throughput = (static_cast<double>(fileSize) / (1024.0 * 1024.0)) / (static_cast<double>(parseTime.count()) / 1000000.0);

        spdlog::debug("Parse complete: {} - {:.2f} MB/s",
            filePath.filename().string(), throughput);
    }

    // Integration with BinaryUtils for automatic tracking
    static void trackRead(size_t bytes, const std::string& description = "") {
        // Simple tracking - just log significant skips
        if (!description.empty() && description.find("skip") != std::string::npos && bytes > 16) {
            spdlog::trace("Skipped {} bytes: {}", bytes, description);
        }
    }
};

// Convenience macro for automatic timing
#define READER_PARSE_TIMER() geck::ReaderDiagnostics::ParseTimer timer__;

} // namespace geck