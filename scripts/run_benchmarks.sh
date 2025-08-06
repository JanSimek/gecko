#!/bin/bash
#
# Run performance benchmarks and track results
# Usage: ./scripts/run_benchmarks.sh [options]
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
BENCHMARK_SAMPLES="${BENCHMARK_SAMPLES:-100}"
BENCHMARK_WARMUP="${BENCHMARK_WARMUP:-100}"
OUTPUT_DIR="${OUTPUT_DIR:-$BUILD_DIR/benchmark_results}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Parse command line arguments
COMPARE_WITH_BASELINE=false
UPDATE_BASELINE=false
VERBOSE=false
THRESHOLD=0.05

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--compare)
            COMPARE_WITH_BASELINE=true
            shift
            ;;
        -u|--update-baseline)
            UPDATE_BASELINE=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -t|--threshold)
            THRESHOLD="$2"
            shift 2
            ;;
        -s|--samples)
            BENCHMARK_SAMPLES="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -c, --compare          Compare with baseline"
            echo "  -u, --update-baseline  Update baseline with current results"
            echo "  -v, --verbose          Verbose output"
            echo "  -t, --threshold PCT    Regression threshold (default: 0.05 = 5%)"
            echo "  -s, --samples N        Number of benchmark samples (default: 100)"
            echo "  -h, --help             Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found: $BUILD_DIR"
    echo "Please build the project first or set BUILD_DIR environment variable"
    exit 1
fi

# Check if performance tests exist
PERF_TEST_EXEC="$BUILD_DIR/performance_tests"
if [ ! -x "$PERF_TEST_EXEC" ]; then
    # Try alternative locations
    for alt in "$BUILD_DIR/tests/performance_tests" "$BUILD_DIR/bin/performance_tests"; do
        if [ -x "$alt" ]; then
            PERF_TEST_EXEC="$alt"
            break
        fi
    done
fi

if [ ! -x "$PERF_TEST_EXEC" ]; then
    echo "Error: Performance test executable not found"
    echo "Looked in: $BUILD_DIR/performance_tests"
    exit 1
fi

echo "Running performance benchmarks..."
echo "Executable: $PERF_TEST_EXEC"
echo "Samples: $BENCHMARK_SAMPLES"
echo "Output directory: $OUTPUT_DIR"
echo

# Run benchmarks
XML_OUTPUT="$OUTPUT_DIR/results_${TIMESTAMP}.xml"
JSON_OUTPUT="$OUTPUT_DIR/results_${TIMESTAMP}.json"
MARKDOWN_OUTPUT="$OUTPUT_DIR/report_${TIMESTAMP}.md"

if [ "$VERBOSE" = true ]; then
    "$PERF_TEST_EXEC" "[performance]" \
        --benchmark-samples "$BENCHMARK_SAMPLES" \
        --benchmark-warmup-time "$BENCHMARK_WARMUP" \
        --reporter xml \
        --out "$XML_OUTPUT"
else
    "$PERF_TEST_EXEC" "[performance]" \
        --benchmark-samples "$BENCHMARK_SAMPLES" \
        --benchmark-warmup-time "$BENCHMARK_WARMUP" \
        --reporter xml \
        --out "$XML_OUTPUT" \
        > /dev/null 2>&1
fi

if [ $? -ne 0 ]; then
    echo "Error: Benchmark execution failed"
    exit 1
fi

echo "Benchmarks completed. Results saved to: $XML_OUTPUT"

# Analyze results
TRACK_SCRIPT="$SCRIPT_DIR/track_performance.py"
if [ ! -f "$TRACK_SCRIPT" ]; then
    echo "Warning: Performance tracking script not found: $TRACK_SCRIPT"
    exit 0
fi

# Determine baseline location
BASELINE_FILE="$PROJECT_ROOT/tests/performance/baseline.json"
if [ ! -f "$BASELINE_FILE" ]; then
    # Try build directory
    BASELINE_FILE="$BUILD_DIR/tests/performance/baseline.json"
fi

# Run analysis
TRACK_ARGS=("$XML_OUTPUT")
TRACK_ARGS+=("--baseline" "$BASELINE_FILE")
TRACK_ARGS+=("--threshold" "$THRESHOLD")
TRACK_ARGS+=("--json-output" "$JSON_OUTPUT")
TRACK_ARGS+=("--markdown-output" "$MARKDOWN_OUTPUT")

if [ "$UPDATE_BASELINE" = true ]; then
    TRACK_ARGS+=("--update-baseline")
fi

echo
echo "Analyzing performance results..."
python3 "$TRACK_SCRIPT" "${TRACK_ARGS[@]}"

# Create symlinks to latest results
ln -sf "results_${TIMESTAMP}.xml" "$OUTPUT_DIR/latest.xml"
ln -sf "results_${TIMESTAMP}.json" "$OUTPUT_DIR/latest.json"
ln -sf "report_${TIMESTAMP}.md" "$OUTPUT_DIR/latest.md"

echo
echo "Performance report: $MARKDOWN_OUTPUT"
echo "Latest results available at: $OUTPUT_DIR/latest.md"

# If comparing with baseline, show summary
if [ "$COMPARE_WITH_BASELINE" = true ] && [ -f "$MARKDOWN_OUTPUT" ]; then
    echo
    echo "=== Performance Summary ==="
    grep -E "(Regressions:|Improvements:|New benchmarks:)" "$MARKDOWN_OUTPUT" || true
fi