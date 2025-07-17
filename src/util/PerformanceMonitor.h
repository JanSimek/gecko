#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>

namespace geck {

/**
 * @brief Performance monitoring and profiling system
 * 
 * This class provides comprehensive performance monitoring capabilities
 * including timing, memory tracking, and automated performance regression detection.
 */
class PerformanceMonitor {
public:
    using TimePoint = std::chrono::high_resolution_clock::time_point;
    using Duration = std::chrono::duration<double, std::milli>;
    
    struct PerformanceMetric {
        std::string name;
        double averageTime = 0.0;
        double minTime = std::numeric_limits<double>::max();
        double maxTime = 0.0;
        size_t sampleCount = 0;
        double totalTime = 0.0;
        
        void addSample(double time) {
            totalTime += time;
            sampleCount++;
            averageTime = totalTime / sampleCount;
            minTime = std::min(minTime, time);
            maxTime = std::max(maxTime, time);
        }
        
        void reset() {
            averageTime = 0.0;
            minTime = std::numeric_limits<double>::max();
            maxTime = 0.0;
            sampleCount = 0;
            totalTime = 0.0;
        }
    };
    
    using PerformanceCallback = std::function<void(const std::string&, const PerformanceMetric&)>;
    
    // Singleton access
    static PerformanceMonitor& getInstance();
    
    // Performance measurement
    void startTimer(const std::string& name);
    void endTimer(const std::string& name);
    void recordMetric(const std::string& name, double value);
    
    // Memory tracking
    void recordMemoryUsage(const std::string& name, size_t bytes);
    void recordMemoryAllocation(const std::string& name, size_t bytes);
    void recordMemoryDeallocation(const std::string& name, size_t bytes);
    
    // Frame rate monitoring
    void recordFrameTime(double frameTime);
    double getAverageFrameRate() const;
    double getCurrentFrameRate() const;
    
    // Metrics access
    const PerformanceMetric* getMetric(const std::string& name) const;
    std::vector<std::string> getMetricNames() const;
    void clearMetrics();
    void clearMetric(const std::string& name);
    
    // Performance regression detection
    void setPerformanceThreshold(const std::string& name, double thresholdMs);
    void setRegressionCallback(PerformanceCallback callback);
    
    // Reporting
    std::string generateReport() const;
    void exportToFile(const std::string& filename) const;
    
    // Configuration
    void setEnabled(bool enabled) { _enabled = enabled; }
    bool isEnabled() const { return _enabled; }
    void setMaxSamples(size_t maxSamples) { _maxSamples = maxSamples; }
    
private:
    PerformanceMonitor() = default;
    ~PerformanceMonitor() = default;
    
    // Non-copyable
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;
    
    mutable std::mutex _mutex;
    std::unordered_map<std::string, PerformanceMetric> _metrics;
    std::unordered_map<std::string, TimePoint> _activeTimers;
    std::unordered_map<std::string, double> _performanceThresholds;
    std::unordered_map<std::string, size_t> _memoryUsage;
    
    // Frame rate tracking
    std::vector<double> _frameTimeSamples;
    size_t _frameTimeSampleIndex = 0;
    static constexpr size_t FRAME_SAMPLE_COUNT = 60;
    
    PerformanceCallback _regressionCallback;
    bool _enabled = true;
    size_t _maxSamples = 1000;
    
    // Helper methods
    void checkPerformanceRegression(const std::string& name, const PerformanceMetric& metric);
    std::string formatTime(double timeMs) const;
    std::string formatMemory(size_t bytes) const;
};

/**
 * @brief RAII timer for automatic performance measurement
 * 
 * Usage:
 * {
 *     auto timer = PerformanceTimer("function_name");
 *     // ... code to measure ...
 * } // Timer automatically stops and records metric
 */
class PerformanceTimer {
public:
    explicit PerformanceTimer(const std::string& name);
    ~PerformanceTimer();
    
    // Manual control
    void stop();
    void restart();
    
    // Get elapsed time without stopping
    double getElapsedTime() const;
    
private:
    using TimePoint = std::chrono::high_resolution_clock::time_point;
    
    std::string _name;
    TimePoint _startTime;
    bool _stopped = false;
};

/**
 * @brief Memory usage tracker for specific allocations
 */
class MemoryTracker {
public:
    explicit MemoryTracker(const std::string& name);
    ~MemoryTracker();
    
    void recordAllocation(size_t bytes);
    void recordDeallocation(size_t bytes);
    
    size_t getCurrentUsage() const { return _currentUsage; }
    size_t getPeakUsage() const { return _peakUsage; }
    
private:
    std::string _name;
    size_t _currentUsage = 0;
    size_t _peakUsage = 0;
};

// Convenience macros for performance monitoring
#define PERF_TIMER(name) geck::PerformanceTimer _perf_timer(name)
#define PERF_FUNCTION() PERF_TIMER(__FUNCTION__)
#define PERF_SCOPE(name) PERF_TIMER(name)

#define PERF_MEMORY_TRACKER(name) geck::MemoryTracker _mem_tracker(name)

// Conditional performance monitoring (only in debug builds)
#ifdef DEBUG
    #define PERF_TIMER_DEBUG(name) PERF_TIMER(name)
    #define PERF_FUNCTION_DEBUG() PERF_FUNCTION()
#else
    #define PERF_TIMER_DEBUG(name)
    #define PERF_FUNCTION_DEBUG()
#endif

} // namespace geck