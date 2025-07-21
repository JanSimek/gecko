#include "PerformanceMonitor.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace geck {

PerformanceMonitor& PerformanceMonitor::getInstance() {
    static PerformanceMonitor instance;
    return instance;
}

void PerformanceMonitor::startTimer(const std::string& name) {
    if (!_enabled)
        return;

    std::lock_guard<std::mutex> lock(_mutex);
    _activeTimers[name] = std::chrono::high_resolution_clock::now();
}

void PerformanceMonitor::endTimer(const std::string& name) {
    if (!_enabled)
        return;

    auto endTime = std::chrono::high_resolution_clock::now();

    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _activeTimers.find(name);
    if (it == _activeTimers.end()) {
        spdlog::warn("PerformanceMonitor: Timer '{}' was not started", name);
        return;
    }

    auto duration = std::chrono::duration_cast<Duration>(endTime - it->second);
    _activeTimers.erase(it);

    auto& metric = _metrics[name];
    metric.name = name;
    metric.addSample(duration.count());

    checkPerformanceRegression(name, metric);
}

void PerformanceMonitor::recordMetric(const std::string& name, double value) {
    if (!_enabled)
        return;

    std::lock_guard<std::mutex> lock(_mutex);
    auto& metric = _metrics[name];
    metric.name = name;
    metric.addSample(value);

    checkPerformanceRegression(name, metric);
}

void PerformanceMonitor::recordMemoryUsage(const std::string& name, size_t bytes) {
    if (!_enabled)
        return;

    std::lock_guard<std::mutex> lock(_mutex);
    _memoryUsage[name] = bytes;
}

void PerformanceMonitor::recordMemoryAllocation(const std::string& name, size_t bytes) {
    if (!_enabled)
        return;

    std::lock_guard<std::mutex> lock(_mutex);
    _memoryUsage[name] += bytes;
}

void PerformanceMonitor::recordMemoryDeallocation(const std::string& name, size_t bytes) {
    if (!_enabled)
        return;

    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _memoryUsage.find(name);
    if (it != _memoryUsage.end()) {
        it->second = (it->second > bytes) ? (it->second - bytes) : 0;
    }
}

void PerformanceMonitor::recordFrameTime(double frameTime) {
    if (!_enabled)
        return;

    std::lock_guard<std::mutex> lock(_mutex);
    if (_frameTimeSamples.size() < FRAME_SAMPLE_COUNT) {
        _frameTimeSamples.push_back(frameTime);
    } else {
        _frameTimeSamples[_frameTimeSampleIndex] = frameTime;
        _frameTimeSampleIndex = (_frameTimeSampleIndex + 1) % FRAME_SAMPLE_COUNT;
    }
}

double PerformanceMonitor::getAverageFrameRate() const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_frameTimeSamples.empty())
        return 0.0;

    double totalTime = 0.0;
    for (double frameTime : _frameTimeSamples) {
        totalTime += frameTime;
    }

    double averageFrameTime = totalTime / _frameTimeSamples.size();
    return (averageFrameTime > 0.0) ? (1000.0 / averageFrameTime) : 0.0; // Convert ms to FPS
}

double PerformanceMonitor::getCurrentFrameRate() const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_frameTimeSamples.empty())
        return 0.0;

    size_t lastIndex = _frameTimeSampleIndex > 0 ? _frameTimeSampleIndex - 1 : _frameTimeSamples.size() - 1;
    double lastFrameTime = _frameTimeSamples[lastIndex];
    return (lastFrameTime > 0.0) ? (1000.0 / lastFrameTime) : 0.0;
}

const PerformanceMonitor::PerformanceMetric* PerformanceMonitor::getMetric(const std::string& name) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _metrics.find(name);
    return (it != _metrics.end()) ? &it->second : nullptr;
}

std::vector<std::string> PerformanceMonitor::getMetricNames() const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<std::string> names;
    names.reserve(_metrics.size());

    for (const auto& pair : _metrics) {
        names.push_back(pair.first);
    }

    return names;
}

void PerformanceMonitor::clearMetrics() {
    std::lock_guard<std::mutex> lock(_mutex);
    _metrics.clear();
    _activeTimers.clear();
    _frameTimeSamples.clear();
    _frameTimeSampleIndex = 0;
}

void PerformanceMonitor::clearMetric(const std::string& name) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _metrics.find(name);
    if (it != _metrics.end()) {
        it->second.reset();
    }
}

void PerformanceMonitor::setPerformanceThreshold(const std::string& name, double thresholdMs) {
    std::lock_guard<std::mutex> lock(_mutex);
    _performanceThresholds[name] = thresholdMs;
}

void PerformanceMonitor::setRegressionCallback(PerformanceCallback callback) {
    std::lock_guard<std::mutex> lock(_mutex);
    _regressionCallback = callback;
}

std::string PerformanceMonitor::generateReport() const {
    std::lock_guard<std::mutex> lock(_mutex);
    std::ostringstream report;

    report << "=== Performance Monitor Report ===\n\n";

    // Frame rate info
    report << "Frame Rate:\n";
    report << "  Current: " << std::fixed << std::setprecision(2) << getCurrentFrameRate() << " FPS\n";
    report << "  Average: " << std::fixed << std::setprecision(2) << getAverageFrameRate() << " FPS\n\n";

    // Performance metrics
    report << "Performance Metrics:\n";
    for (const auto& pair : _metrics) {
        const auto& metric = pair.second;
        report << "  " << metric.name << ":\n";
        report << "    Average: " << formatTime(metric.averageTime) << "\n";
        report << "    Min: " << formatTime(metric.minTime) << "\n";
        report << "    Max: " << formatTime(metric.maxTime) << "\n";
        report << "    Samples: " << metric.sampleCount << "\n";
        report << "    Total: " << formatTime(metric.totalTime) << "\n\n";
    }

    // Memory usage
    if (!_memoryUsage.empty()) {
        report << "Memory Usage:\n";
        for (const auto& pair : _memoryUsage) {
            report << "  " << pair.first << ": " << formatMemory(pair.second) << "\n";
        }
        report << "\n";
    }

    return report.str();
}

void PerformanceMonitor::exportToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        spdlog::error("PerformanceMonitor: Failed to open file '{}' for export", filename);
        return;
    }

    file << generateReport();
    file.close();

    spdlog::info("PerformanceMonitor: Exported performance report to '{}'", filename);
}

void PerformanceMonitor::checkPerformanceRegression(const std::string& name, const PerformanceMetric& metric) {
    auto thresholdIt = _performanceThresholds.find(name);
    if (thresholdIt != _performanceThresholds.end()) {
        if (metric.averageTime > thresholdIt->second) {
            spdlog::warn("Performance regression detected for '{}': {:.2f}ms > {:.2f}ms threshold",
                name, metric.averageTime, thresholdIt->second);

            if (_regressionCallback) {
                _regressionCallback(name, metric);
            }
        }
    }
}

std::string PerformanceMonitor::formatTime(double timeMs) const {
    std::ostringstream oss;
    if (timeMs < 1.0) {
        oss << std::fixed << std::setprecision(3) << timeMs << "ms";
    } else if (timeMs < 1000.0) {
        oss << std::fixed << std::setprecision(2) << timeMs << "ms";
    } else {
        oss << std::fixed << std::setprecision(2) << (timeMs / 1000.0) << "s";
    }
    return oss.str();
}

std::string PerformanceMonitor::formatMemory(size_t bytes) const {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double size = static_cast<double>(bytes);
    size_t unitIndex = 0;

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
    return oss.str();
}

// PerformanceTimer implementation
PerformanceTimer::PerformanceTimer(const std::string& name)
    : _name(name) {
    restart();
}

PerformanceTimer::~PerformanceTimer() {
    if (!_stopped) {
        stop();
    }
}

void PerformanceTimer::stop() {
    if (_stopped)
        return;

    auto& monitor = PerformanceMonitor::getInstance();
    monitor.endTimer(_name);
    _stopped = true;
}

void PerformanceTimer::restart() {
    auto& monitor = PerformanceMonitor::getInstance();
    monitor.startTimer(_name);
    _startTime = std::chrono::high_resolution_clock::now();
    _stopped = false;
}

double PerformanceTimer::getElapsedTime() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<PerformanceMonitor::Duration>(now - _startTime);
    return duration.count();
}

// MemoryTracker implementation
MemoryTracker::MemoryTracker(const std::string& name)
    : _name(name) {
    auto& monitor = PerformanceMonitor::getInstance();
    monitor.recordMemoryUsage(_name, 0);
}

MemoryTracker::~MemoryTracker() {
    auto& monitor = PerformanceMonitor::getInstance();
    monitor.recordMemoryUsage(_name, 0);
}

void MemoryTracker::recordAllocation(size_t bytes) {
    _currentUsage += bytes;
    _peakUsage = std::max(_peakUsage, _currentUsage);

    auto& monitor = PerformanceMonitor::getInstance();
    monitor.recordMemoryUsage(_name, _currentUsage);
}

void MemoryTracker::recordDeallocation(size_t bytes) {
    _currentUsage = (_currentUsage > bytes) ? (_currentUsage - bytes) : 0;

    auto& monitor = PerformanceMonitor::getInstance();
    monitor.recordMemoryUsage(_name, _currentUsage);
}

} // namespace geck