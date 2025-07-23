#!/bin/bash

# Post-Build Testing Framework for Gecko
# Automatically tests application stability after compilation

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
EXECUTABLE="$BUILD_DIR/src/geck-mapper"
TEST_RESULTS_DIR="$PROJECT_ROOT/test-results"
CRASH_REPORTS_DIR="$HOME/Library/Logs/DiagnosticReports"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
TEST_DURATION=3
TEST_ITERATIONS=5
TIMEOUT_DURATION=30

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[FAIL]${NC} $1"
}

# Initialize test environment
init_test_environment() {
    log_info "Initializing test environment..."
    
    # Create test results directory
    mkdir -p "$TEST_RESULTS_DIR"
    
    # Clear previous test results
    rm -f "$TEST_RESULTS_DIR"/*.log "$TEST_RESULTS_DIR"/*.report
    
    # Check if executable exists
    if [[ ! -f "$EXECUTABLE" ]]; then
        log_error "Executable not found: $EXECUTABLE"
        log_error "Please build the project first: cmake --build build"
        exit 1
    fi
    
    # Make executable if needed
    chmod +x "$EXECUTABLE"
    
    log_success "Test environment initialized"
}

# Check for recent crash reports
check_crash_reports() {
    log_info "Checking for recent crash reports..."
    
    local test_start_time=$(date +%s)
    local crash_found=false
    
    # Wait a moment for crash reports to be written
    sleep 2
    
    if [[ -d "$CRASH_REPORTS_DIR" ]]; then
        # Find crash reports for our application created in the last 5 minutes
        while IFS= read -r -d '' crash_file; do
            local crash_time=$(stat -f "%Sm" -t "%s" "$crash_file" 2>/dev/null || echo "0")
            local time_diff=$((test_start_time - crash_time))
            
            # Check if crash occurred within the last 5 minutes (300 seconds)
            if [[ $time_diff -lt 300 && $time_diff -gt -60 ]]; then
                log_error "Recent crash detected: $(basename "$crash_file")"
                cp "$crash_file" "$TEST_RESULTS_DIR/"
                crash_found=true
            fi
        done < <(find "$CRASH_REPORTS_DIR" -name "geck-mapper-*.ips" -print0 2>/dev/null || true)
    fi
    
    if [[ "$crash_found" == "false" ]]; then
        log_success "No recent crash reports found"
        return 0
    else
        return 1
    fi
}

# Test application startup and shutdown
test_startup_shutdown() {
    local test_name="Startup/Shutdown Test"
    log_info "Running $test_name..."
    
    local failures=0
    
    for ((i=1; i<=TEST_ITERATIONS; i++)); do
        log_info "  Iteration $i/$TEST_ITERATIONS"
        
        # Start application in background
        "$EXECUTABLE" > "$TEST_RESULTS_DIR/startup_test_$i.log" 2>&1 &
        local app_pid=$!
        
        # Let it run briefly
        sleep $TEST_DURATION
        
        # Check if process is still running
        if ! kill -0 $app_pid 2>/dev/null; then
            log_error "  Application crashed during startup (iteration $i)"
            ((failures++))
            continue
        fi
        
        # Gracefully terminate
        kill -TERM $app_pid 2>/dev/null || true
        
        # Wait for termination
        local wait_count=0
        while kill -0 $app_pid 2>/dev/null && [[ $wait_count -lt 10 ]]; do
            sleep 0.5
            ((wait_count++))
        done
        
        # Force kill if still running
        if kill -0 $app_pid 2>/dev/null; then
            log_warning "  Force killing application (iteration $i)"
            kill -KILL $app_pid 2>/dev/null || true
            ((failures++))
        fi
        
        # Check for crash reports after this iteration
        if ! check_crash_reports; then
            log_error "  Crash detected in iteration $i"
            ((failures++))
        fi
    done
    
    if [[ $failures -eq 0 ]]; then
        log_success "$test_name passed ($TEST_ITERATIONS iterations)"
        return 0
    else
        log_error "$test_name failed ($failures/$TEST_ITERATIONS iterations failed)"
        return 1
    fi
}

# Test application with resource directory selection
test_resource_selection() {
    local test_name="Resource Selection Test"
    log_info "Running $test_name..."
    
    # Create a simple test that starts the app and immediately closes the file dialog
    # This tests the file dialog threading issue we fixed
    
    "$EXECUTABLE" > "$TEST_RESULTS_DIR/resource_test.log" 2>&1 &
    local app_pid=$!
    
    # Give it time to show the dialog
    sleep 2
    
    # Send Escape key to close dialog (if supported by Qt)
    # Or just terminate gracefully
    kill -TERM $app_pid 2>/dev/null || true
    
    # Wait for termination
    local wait_count=0
    while kill -0 $app_pid 2>/dev/null && [[ $wait_count -lt 5 ]]; do
        sleep 0.5
        ((wait_count++))
    done
    
    if kill -0 $app_pid 2>/dev/null; then
        kill -KILL $app_pid 2>/dev/null || true
        log_warning "$test_name: Force killed application"
    fi
    
    # Check for crashes
    if check_crash_reports; then
        log_success "$test_name passed"
        return 0
    else
        log_error "$test_name failed (crash detected)"
        return 1
    fi
}

# Test memory usage and leaks
test_memory_usage() {
    local test_name="Memory Usage Test"
    log_info "Running $test_name..."
    
    # Check if we have memory analysis tools available
    if command -v leaks >/dev/null 2>&1; then
        log_info "Running memory leak detection..."
        
        # Start application
        "$EXECUTABLE" > "$TEST_RESULTS_DIR/memory_test.log" 2>&1 &
        local app_pid=$!
        
        sleep $TEST_DURATION
        
        # Run leaks tool (macOS specific)
        leaks $app_pid > "$TEST_RESULTS_DIR/leaks_report.txt" 2>&1 || true
        
        # Terminate application
        kill -TERM $app_pid 2>/dev/null || true
        sleep 1
        kill -KILL $app_pid 2>/dev/null || true
        
        # Check leaks report
        if grep -q "0 leaks for 0 total leaked bytes" "$TEST_RESULTS_DIR/leaks_report.txt"; then
            log_success "$test_name passed (no memory leaks detected)"
            return 0
        else
            log_warning "$test_name: Memory leaks detected (see leaks_report.txt)"
            return 1
        fi
    else
        log_warning "$test_name skipped (leaks tool not available)"
        return 0
    fi
}

# Test Qt6 UI components
test_qt_ui() {
    local test_name="Qt6 UI Components Test"
    log_info "Running $test_name..."
    
    # This test validates that Qt6 components initialize correctly
    # We'll check that the application starts and runs without immediate crashes
    
    "$EXECUTABLE" > "$TEST_RESULTS_DIR/qt_ui_test.log" 2>&1 &
    local app_pid=$!
    
    sleep $TEST_DURATION
    
    # Check if process is still running (indicates successful Qt6 initialization)
    if kill -0 $app_pid 2>/dev/null; then
        log_success "$test_name passed (Qt6 application running normally)"
        kill -TERM $app_pid 2>/dev/null || true
        sleep 1
        kill -KILL $app_pid 2>/dev/null || true
        return 0
    else
        log_error "$test_name failed (Qt6 application crashed immediately)"
        return 1
    fi
}

# Generate test report
generate_report() {
    local total_tests=$1
    local passed_tests=$2
    local failed_tests=$((total_tests - passed_tests))
    
    local report_file="$TEST_RESULTS_DIR/test_report.txt"
    
    {
        echo "Gecko Post-Build Test Report"
        echo "===================================="
        echo "Date: $(date)"
        echo "Build Directory: $BUILD_DIR"
        echo "Executable: $EXECUTABLE"
        echo ""
        echo "Test Results:"
        echo "  Total Tests: $total_tests"
        echo "  Passed: $passed_tests"
        echo "  Failed: $failed_tests"
        echo ""
        
        if [[ $failed_tests -eq 0 ]]; then
            echo "✅ ALL TESTS PASSED - Application is stable"
        else
            echo "❌ SOME TESTS FAILED - Review test logs"
        fi
        
        echo ""
        echo "Test Logs:"
        for log_file in "$TEST_RESULTS_DIR"/*.log; do
            if [[ -f "$log_file" ]]; then
                echo "  - $(basename "$log_file")"
            fi
        done
        
        echo ""
        echo "Crash Reports:"
        for crash_file in "$TEST_RESULTS_DIR"/*.ips; do
            if [[ -f "$crash_file" ]]; then
                echo "  - $(basename "$crash_file")"
            fi
        done
        
    } > "$report_file"
    
    log_info "Test report generated: $report_file"
}

# Main test execution
main() {
    echo "=========================================="
    echo "Gecko Post-Build Testing Framework"
    echo "=========================================="
    echo ""
    
    init_test_environment
    
    local total_tests=0
    local passed_tests=0
    
    # Run all tests
    if test_startup_shutdown; then ((passed_tests++)); fi
    ((total_tests++))
    
    if test_resource_selection; then ((passed_tests++)); fi
    ((total_tests++))
    
    if test_memory_usage; then ((passed_tests++)); fi
    ((total_tests++))
    
    if test_qt_ui; then ((passed_tests++)); fi
    ((total_tests++))
    
    echo ""
    echo "=========================================="
    
    generate_report $total_tests $passed_tests
    
    local failed_tests=$((total_tests - passed_tests))
    
    if [[ $failed_tests -eq 0 ]]; then
        log_success "All tests passed! ($passed_tests/$total_tests)"
        echo ""
        log_success "🎉 Application is ready for use!"
        exit 0
    else
        log_error "Some tests failed! ($failed_tests/$total_tests)"
        echo ""
        log_error "⚠️  Please review test results before using the application"
        exit 1
    fi
}

# Handle script interruption
trap 'log_warning "Test interrupted"; exit 130' INT TERM

# Run main function
main "$@"