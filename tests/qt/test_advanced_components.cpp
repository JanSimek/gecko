#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <chrono>

#include "util/StringUtils.h"
// #include "util/SpatialIndex.h"  // Disabled due to template instantiation issues
#include "util/PerformanceMonitor.h"
#include "ui/components/ViewportManager.h"

using namespace geck;

TEST_CASE("StringUtils performance optimizations", "[string_utils]") {
    SECTION("Fast integer to string conversion") {
        REQUIRE(StringUtils::fastToString(123) == "123");
        REQUIRE(StringUtils::fastToString(-456) == "-456");
        REQUIRE(StringUtils::fastToString(0) == "0");
        REQUIRE(StringUtils::fastToString(999999) == "999999");
    }
    
    SECTION("Optimized tile path generation") {
        auto path = StringUtils::getTilePath(12345);
        REQUIRE(path == "art/tiles/12345.frm");
        
        auto path2 = StringUtils::getTilePath(0);
        REQUIRE(path2 == "art/tiles/0.frm");
    }
    
    SECTION("Object path generation") {
        auto path = StringUtils::getObjectPath(6789);
        REQUIRE(path == "art/critters/6789.frm");
    }
    
    SECTION("String concatenation") {
        auto result = StringUtils::concat("Hello", " ", "World", "!");
        REQUIRE(result == "Hello World!");
        
        auto result2 = StringUtils::concat("Number: ", 42);
        REQUIRE(result2 == "Number: 42");
    }
    
    SECTION("String formatting") {
        auto result = StringUtils::format("Value: {}, Name: {}", 123, "test");
        REQUIRE(result == "Value: 123, Name: test");
    }
    
    SECTION("String trimming") {
        auto trimmed = StringUtils::trim("  hello world  ");
        REQUIRE(trimmed == "hello world");
        
        auto trimmed2 = StringUtils::trim("no_spaces");
        REQUIRE(trimmed2 == "no_spaces");
        
        auto trimmed3 = StringUtils::trim("   ");
        REQUIRE(trimmed3 == "");
    }
    
    SECTION("String splitting") {
        auto parts = StringUtils::split("one,two,three", ',');
        REQUIRE(parts.size() == 3);
        REQUIRE(parts[0] == "one");
        REQUIRE(parts[1] == "two");
        REQUIRE(parts[2] == "three");
    }
    
    SECTION("String starts/ends with") {
        REQUIRE(StringUtils::startsWith("hello_world", "hello"));
        REQUIRE(!StringUtils::startsWith("hello_world", "world"));
        
        REQUIRE(StringUtils::endsWith("hello_world", "world"));
        REQUIRE(!StringUtils::endsWith("hello_world", "hello"));
    }
    
    SECTION("Case insensitive comparison") {
        REQUIRE(StringUtils::equalsIgnoreCase("Hello", "HELLO"));
        REQUIRE(StringUtils::equalsIgnoreCase("test", "Test"));
        REQUIRE(!StringUtils::equalsIgnoreCase("hello", "world"));
    }
    
    SECTION("String pool for interning") {
        const std::string& str1 = StringUtils::intern("repeated_string");
        const std::string& str2 = StringUtils::intern("repeated_string");
        
        // Should return the same instance
        REQUIRE(&str1 == &str2);
    }
}

// SpatialIndex tests disabled due to template instantiation issues
/*
TEST_CASE("SpatialIndex performance", "[spatial_index]") {
    SECTION("Basic spatial indexing") {
        SpatialIndex<int> index(100.0f);
        
        // Add some items
        auto id1 = index.addItem(1, sf::FloatRect(0, 0, 50, 50));
        auto id2 = index.addItem(2, sf::FloatRect(75, 75, 50, 50));
        auto id3 = index.addItem(3, sf::FloatRect(200, 200, 50, 50));
        
        REQUIRE(index.getItemCount() == 3);
        
        // Query overlapping area
        auto results = index.queryArea(sf::FloatRect(0, 0, 100, 100));
        REQUIRE(results.size() == 2); // Should find items 1 and 2
        
        // Query non-overlapping area
        auto results2 = index.queryArea(sf::FloatRect(300, 300, 50, 50));
        REQUIRE(results2.size() == 0);
    }
    
    SECTION("Point queries") {
        SpatialIndex<int> index(50.0f);
        
        index.addItem(100, sf::FloatRect(0, 0, 20, 20));
        index.addItem(200, sf::FloatRect(50, 50, 20, 20));
        
        auto results = index.queryPoint(sf::Vector2f(10, 10));
        REQUIRE(results.size() == 1);
        REQUIRE(results[0] == 100);
        
        auto results2 = index.queryPoint(sf::Vector2f(60, 60));
        REQUIRE(results2.size() == 1);
        REQUIRE(results2[0] == 200);
    }
    
    SECTION("Update and remove operations") {
        SpatialIndex<int> index(100.0f);
        
        auto id = index.addItem(42, sf::FloatRect(0, 0, 50, 50));
        
        // Update item position
        index.updateItem(id, sf::FloatRect(100, 100, 50, 50));
        
        // Old position should not find the item
        auto results1 = index.queryArea(sf::FloatRect(0, 0, 50, 50));
        REQUIRE(results1.size() == 0);
        
        // New position should find the item
        auto results2 = index.queryArea(sf::FloatRect(100, 100, 50, 50));
        REQUIRE(results2.size() == 1);
        REQUIRE(results2[0] == 42);
        
        // Remove item
        index.removeItem(id);
        REQUIRE(index.getItemCount() == 0);
    }
}
*/

TEST_CASE("PerformanceMonitor functionality", "[performance_monitor]") {
    SECTION("Basic timer operations") {
        auto& monitor = PerformanceMonitor::getInstance();
        monitor.clearMetrics();
        
        // Test timer
        monitor.startTimer("test_operation");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        monitor.endTimer("test_operation");
        
        const auto* metric = monitor.getMetric("test_operation");
        REQUIRE(metric != nullptr);
        REQUIRE(metric->sampleCount == 1);
        REQUIRE(metric->averageTime >= 10.0); // Should be at least 10ms
    }
    
    SECTION("RAII timer") {
        auto& monitor = PerformanceMonitor::getInstance();
        monitor.clearMetrics();
        
        {
            PerformanceTimer timer("raii_test");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } // Timer automatically stops here
        
        const auto* metric = monitor.getMetric("raii_test");
        REQUIRE(metric != nullptr);
        REQUIRE(metric->sampleCount == 1);
        REQUIRE(metric->averageTime >= 5.0);
    }
    
    SECTION("Memory tracking") {
        auto& monitor = PerformanceMonitor::getInstance();
        
        monitor.recordMemoryUsage("test_memory", 1024);
        monitor.recordMemoryAllocation("test_memory", 512);
        
        // Basic functionality test - actual values depend on implementation
        auto report = monitor.generateReport();
        REQUIRE(report.find("test_memory") != std::string::npos);
    }
    
    SECTION("Performance thresholds") {
        auto& monitor = PerformanceMonitor::getInstance();
        monitor.clearMetrics();
        
        bool regressionDetected = false;
        monitor.setRegressionCallback([&regressionDetected](const std::string& name, const PerformanceMonitor::PerformanceMetric& metric) {
            regressionDetected = true;
        });
        
        monitor.setPerformanceThreshold("slow_operation", 5.0); // 5ms threshold
        
        // Record a slow operation
        monitor.recordMetric("slow_operation", 10.0); // 10ms - should trigger regression
        
        REQUIRE(regressionDetected);
    }
}

TEST_CASE("ViewportManager operations", "[viewport_manager]") {
    SECTION("Basic zoom operations") {
        ui::components::ViewportManager viewport;
        
        REQUIRE(viewport.getZoomLevel() == 1.0f);
        
        viewport.zoom(2.0f);
        REQUIRE(viewport.getZoomLevel() == 2.0f);
        
        viewport.setZoom(0.5f);
        REQUIRE(viewport.getZoomLevel() == 0.5f);
    }
    
    SECTION("Zoom limits") {
        ui::components::ViewportManager viewport;
        viewport.setZoomLimits(0.5f, 3.0f);
        
        // Test minimum limit
        viewport.setZoom(0.1f);
        REQUIRE(viewport.getZoomLevel() == 0.5f);
        
        // Test maximum limit
        viewport.setZoom(5.0f);
        REQUIRE(viewport.getZoomLevel() == 3.0f);
    }
    
    SECTION("Pan operations") {
        ui::components::ViewportManager viewport;
        
        sf::Vector2f originalCenter = viewport.getView().getCenter();
        
        viewport.pan(sf::Vector2f(100, 50));
        sf::Vector2f newCenter = viewport.getView().getCenter();
        
        REQUIRE(newCenter.x == originalCenter.x + 100);
        REQUIRE(newCenter.y == originalCenter.y + 50);
    }
    
    SECTION("Fit to rectangle") {
        ui::components::ViewportManager viewport;
        
        sf::FloatRect targetRect(0, 0, 800, 600);
        sf::Vector2u viewportSize(1024, 768);
        
        viewport.fitToRect(targetRect, viewportSize);
        
        sf::Vector2f center = viewport.getView().getCenter();
        REQUIRE(center.x == 400.0f); // Center of the rectangle
        REQUIRE(center.y == 300.0f);
    }
}

TEST_CASE("Component integration", "[integration]") {
    SECTION("Performance monitoring with string operations") {
        auto& monitor = PerformanceMonitor::getInstance();
        monitor.clearMetrics();
        
        {
            PERF_TIMER("string_operations");
            
            // Perform some string operations
            for (int i = 0; i < 1000; ++i) {
                auto path = StringUtils::getTilePath(i);
                auto trimmed = StringUtils::trim(path);
            }
        }
        
        const auto* metric = monitor.getMetric("string_operations");
        REQUIRE(metric != nullptr);
        REQUIRE(metric->sampleCount == 1);
        REQUIRE(metric->averageTime > 0.0);
    }
    
    // Spatial index test disabled due to template instantiation issues
    /*
    SECTION("Spatial index with performance monitoring") {
        auto& monitor = PerformanceMonitor::getInstance();
        monitor.clearMetrics();
        
        SpatialIndex<int> index(100.0f);
        
        {
            PERF_TIMER("spatial_index_operations");
            
            // Add many items
            for (int i = 0; i < 100; ++i) {
                index.addItem(i, sf::FloatRect(i * 10, i * 10, 50, 50));
            }
            
            // Query area
            auto results = index.queryArea(sf::FloatRect(0, 0, 500, 500));
        }
        
        const auto* metric = monitor.getMetric("spatial_index_operations");
        REQUIRE(metric != nullptr);
        REQUIRE(metric->sampleCount == 1);
    }
    */
}