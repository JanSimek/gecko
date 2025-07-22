#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

#include "reader/dat/DatReader.h"
#include "reader/pro/ProReader.h"
#include "reader/frm/FrmReader.h"
#include "reader/pal/PalReader.h"
#include "reader/gam/GamReader.h"
#include "reader/msg/MsgReader.h"
#include "reader/lst/LstReader.h"
#include "reader/ReaderDiagnostics.h"

// Include format classes for complete type definitions
#include "format/dat/Dat.h"
#include "format/pro/Pro.h"
#include "format/frm/Frm.h"
#include "format/pal/Pal.h"
#include "format/gam/Gam.h"
#include "format/msg/Msg.h"
#include "format/lst/Lst.h"

namespace geck::test {

// Helper to create test data of various sizes
std::vector<uint8_t> createTestData(size_t size, uint8_t pattern = 0xAB) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>((pattern + i) % 256);
    }
    return data;
}

// Helper to create minimal valid PRO file data
std::vector<uint8_t> createValidProData() {
    std::vector<uint8_t> data;
    
    // Header (24 bytes)
    // PID (4 bytes, big endian) - MISC type (5 << 24)
    data.insert(data.end(), {0x05, 0x00, 0x00, 0x00});
    
    // Message ID (4 bytes)
    data.insert(data.end(), {0x00, 0x00, 0x00, 0x64});
    
    // FID (4 bytes) - -1 for no FRM
    data.insert(data.end(), {0xFF, 0xFF, 0xFF, 0xFF});
    
    // Light distance (4 bytes)
    data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
    
    // Light intensity (4 bytes)
    data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
    
    // Flags (4 bytes)
    data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
    
    // MISC type requires additional 4 bytes (unknown field)
    data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
    
    return data;
}

// Helper to create valid FRM file data
std::vector<uint8_t> createValidFrmData() {
    std::vector<uint8_t> data;
    
    // Version (4 bytes) = 4
    data.insert(data.end(), {0x00, 0x00, 0x00, 0x04});
    
    // FPS (2 bytes) = 10
    data.insert(data.end(), {0x00, 0x0A});
    
    // Action frame (2 bytes) = 0
    data.insert(data.end(), {0x00, 0x00});
    
    // Frames per direction (2 bytes) = 1
    data.insert(data.end(), {0x00, 0x01});
    
    // Direction shift X array (6 * 2 bytes)
    for (int i = 0; i < 6; ++i) {
        data.insert(data.end(), {0x00, 0x00});
    }
    
    // Direction shift Y array (6 * 2 bytes)
    for (int i = 0; i < 6; ++i) {
        data.insert(data.end(), {0x00, 0x00});
    }
    
    // Data offset array (6 * 4 bytes) - all point to same data
    uint32_t offset = 0; // Offset from start of frame data
    for (int i = 0; i < 6; ++i) {
        data.insert(data.end(), {
            static_cast<uint8_t>(offset >> 24),
            static_cast<uint8_t>(offset >> 16),
            static_cast<uint8_t>(offset >> 8),
            static_cast<uint8_t>(offset)
        });
    }
    
    // Frame data size (4 bytes) = 12 bytes (frame header + 4 pixels)
    data.insert(data.end(), {0x00, 0x00, 0x00, 0x10});
    
    // Frame data starts here
    // Frame width (2 bytes) = 2
    data.insert(data.end(), {0x00, 0x02});
    
    // Frame height (2 bytes) = 2  
    data.insert(data.end(), {0x00, 0x02});
    
    // Pixel count (4 bytes) = 4
    data.insert(data.end(), {0x00, 0x00, 0x00, 0x04});
    
    // Offset X (2 bytes) = 0
    data.insert(data.end(), {0x00, 0x00});
    
    // Offset Y (2 bytes) = 0
    data.insert(data.end(), {0x00, 0x00});
    
    // Pixel data (4 bytes)
    data.insert(data.end(), {0xFF, 0x00, 0xFF, 0x00});
    
    return data;
}

// Helper to measure memory usage
struct MemoryUsage {
    size_t peak_memory_kb = 0;
    size_t allocations = 0;
};

class PerformanceTimer {
public:
    PerformanceTimer() : start_time(std::chrono::high_resolution_clock::now()) {}
    
    std::chrono::microseconds elapsed() {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    }
    
    double throughput_mbps(size_t bytes) {
        auto elapsed_us = elapsed();
        if (elapsed_us.count() == 0) return 0.0;
        
        double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        double seconds = static_cast<double>(elapsed_us.count()) / 1000000.0;
        return mb / seconds;
    }

private:
    std::chrono::high_resolution_clock::time_point start_time;
};

} // namespace geck::test

TEST_CASE("ProReader Performance", "[performance][pro]") {
    using namespace geck::test;
    
    SECTION("Small PRO File Performance") {
        auto pro_data = createValidProData();
        
        BENCHMARK("ProReader - Small File (24 bytes)") {
            geck::ProReader reader;
            return reader.openFile("test.pro", pro_data);
        };
    }
    
    SECTION("Batch PRO File Performance") {
        auto pro_data = createValidProData();
        
        BENCHMARK("ProReader - Batch Processing (100 files)") {
            std::vector<std::unique_ptr<geck::Pro>> results;
            results.reserve(100);
            
            for (int i = 0; i < 100; ++i) {
                geck::ProReader reader;
                results.push_back(reader.openFile("test.pro", pro_data));
            }
            
            return results.size();
        };
    }
}

TEST_CASE("FrmReader Performance", "[performance][frm]") {
    using namespace geck::test;
    
    SECTION("Small FRM File Performance") {
        auto frm_data = createValidFrmData();
        
        BENCHMARK("FrmReader - Small Animation") {
            geck::FrmReader reader;
            return reader.openFile("test.frm", frm_data);
        };
    }
    
    SECTION("FrmReader Throughput") {
        auto frm_data = createValidFrmData();
        
        geck::FrmReader reader;
        PerformanceTimer timer;
        auto result = reader.openFile("test.frm", frm_data);
        double throughput = timer.throughput_mbps(frm_data.size());
        
        REQUIRE(result != nullptr);
        INFO("FRM reading throughput: " << throughput << " MB/s");
    }
}

TEST_CASE("GamReader Performance", "[performance][gam]") {
    // Test with actual GAM file if available
    if (std::filesystem::exists("data/test.gam")) {
        BENCHMARK("GamReader - Real File") {
            geck::GamReader reader;
            return reader.openFile("data/test.gam");
        };
    }
}

TEST_CASE("MsgReader Performance", "[performance][msg]") {
    // Test with actual MSG file if available
    if (std::filesystem::exists("data/test.msg")) {
        BENCHMARK("MsgReader - Real File") {
            geck::MsgReader reader;
            return reader.openFile("data/test.msg");
        };
    }
}

TEST_CASE("Reader Diagnostics Performance", "[performance][diagnostics]") {
    using namespace geck;
    
    SECTION("Diagnostics Overhead") {
        auto pro_data = geck::test::createValidProData();
        
        // Test without diagnostics
        BENCHMARK("ProReader without logging") {
            ProReader reader;
            return reader.openFile("test.pro", pro_data);
        };
        
        // Test with diagnostics - skip file size check for in-memory data
        BENCHMARK("ProReader with diagnostics") {
            ProReader reader;
            // Skip diagnostics calls that require actual file paths
            auto result = reader.openFile("test.pro", pro_data);
            return result;
        };
    }
}

TEST_CASE("Memory Usage Tests", "[performance][memory]") {
    using namespace geck::test;
    
    SECTION("PRO Memory Usage") {
        auto pro_data = createValidProData();
        
        // Simple memory usage test
        std::vector<std::unique_ptr<geck::Pro>> objects;
        
        for (int i = 0; i < 1000; ++i) {
            geck::ProReader reader;
            objects.push_back(reader.openFile("test.pro", pro_data));
        }
        
        REQUIRE(objects.size() == 1000);
        INFO("Created 1000 PRO objects in memory");
        
        objects.clear(); // Force cleanup
    }
}

TEST_CASE("Error Handling Performance", "[performance][errors]") {
    using namespace geck::test;
    
    SECTION("Invalid Data Performance") {
        // Test performance when handling invalid data
        auto invalid_data = createTestData(10); // Too small for any valid format
        
        BENCHMARK("ProReader - Invalid Data Error Handling") {
            geck::ProReader reader;
            try {
                return reader.openFile("invalid.pro", invalid_data);
            } catch (const std::exception&) {
                return std::unique_ptr<geck::Pro>(nullptr);
            }
        };
    }
    
    SECTION("Exception vs Error Code Performance") {
        // This would be useful for comparing exception-based vs error-code based error handling
        // Currently we use exceptions, but we could benchmark alternatives
        auto invalid_data = createTestData(10);
        
        int error_count = 0;
        
        BENCHMARK("Error handling with exceptions") {
            geck::ProReader reader;
            try {
                reader.openFile("invalid.pro", invalid_data);
            } catch (const std::exception&) {
                error_count++;
            }
            return error_count;
        };
    }
}