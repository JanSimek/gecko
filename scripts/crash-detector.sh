#!/bin/bash

# Crash Detection Utility for Gecko
# Monitors and analyzes application crashes

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
CRASH_REPORTS_DIR="$HOME/Library/Logs/DiagnosticReports"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Analyze crash report
analyze_crash_report() {
    local crash_file="$1"
    local output_file="$2"
    
    {
        echo "Crash Analysis for: $(basename "$crash_file")"
        echo "============================================"
        echo ""
        
        # Extract basic crash info
        if grep -q "\"exception\"" "$crash_file"; then
            echo "Exception Information:"
            grep -A 5 "\"exception\"" "$crash_file" | sed 's/^/  /'
            echo ""
        fi
        
        # Extract stack trace from main thread
        if grep -q "\"triggered\":true" "$crash_file"; then
            echo "Main Thread Stack Trace:"
            awk '/\"triggered\":true/,/^\s*\}/' "$crash_file" | 
            grep -A 20 "\"frames\"" | 
            grep "\"symbol\"" | 
            sed 's/.*"symbol"://; s/,.*//; s/"//g; s/^/  /'
            echo ""
        fi
        
        # Extract binary information
        if grep -q "\"name\":\"gecko\"" "$crash_file"; then
            echo "Application Binary Info:"
            grep -A 5 "\"name\":\"gecko\"" "$crash_file" | sed 's/^/  /'
            echo ""
        fi
        
        # Check for known crash patterns
        echo "Known Issue Analysis:"
        if grep -q "QGuiApplication::~QGuiApplication" "$crash_file"; then
            echo "  ❌ Qt Application Double Destruction Detected"
            echo "     Solution: Ensure QApplication is destroyed only once"
        elif grep -q "~QWidget" "$crash_file"; then
            echo "  ❌ Qt Widget Destruction Issue"
            echo "     Solution: Check widget parent-child relationships"
        elif grep -q "sf::" "$crash_file"; then
            echo "  ❌ SFML Related Crash"
            echo "     Solution: Check SFML-Qt integration in SFMLWidget"
        elif grep -q "std::shared_ptr" "$crash_file"; then
            echo "  ❌ Smart Pointer Issue"
            echo "     Solution: Check shared_ptr lifecycle and circular references"
        else
            echo "  ✓ No known crash patterns detected"
        fi
        
    } > "$output_file"
}

# Find recent crash reports
find_recent_crashes() {
    local hours_ago="${1:-24}"
    local current_time=$(date +%s)
    local cutoff_time=$((current_time - hours_ago * 3600))
    
    log_info "Searching for gecko crashes in the last $hours_ago hours..."
    
    if [[ ! -d "$CRASH_REPORTS_DIR" ]]; then
        log_warning "Crash reports directory not found: $CRASH_REPORTS_DIR"
        return 1
    fi
    
    local crash_count=0
    
    while IFS= read -r -d '' crash_file; do
        local crash_time=$(stat -f "%Sm" -t "%s" "$crash_file" 2>/dev/null || echo "0")
        
        if [[ $crash_time -gt $cutoff_time ]]; then
            local crash_date=$(date -r "$crash_time" '+%Y-%m-%d %H:%M:%S')
            log_error "Crash found: $(basename "$crash_file") at $crash_date"
            
            # Analyze the crash
            local analysis_file="$PROJECT_ROOT/test-results/crash-analysis-$(basename "$crash_file" .ips).txt"
            mkdir -p "$(dirname "$analysis_file")"
            analyze_crash_report "$crash_file" "$analysis_file"
            
            log_info "Analysis saved to: $analysis_file"
            ((crash_count++))
        fi
    done < <(find "$CRASH_REPORTS_DIR" -name "gecko-*.ips" -print0 2>/dev/null || true)
    
    if [[ $crash_count -eq 0 ]]; then
        log_success "No recent crashes found"
        return 0
    else
        log_error "Found $crash_count recent crash(es)"
        return 1
    fi
}

# Monitor for new crashes in real-time
monitor_crashes() {
    log_info "Monitoring for new gecko crashes... (Press Ctrl+C to stop)"
    
    local last_check=$(date +%s)
    
    while true; do
        sleep 5
        local current_time=$(date +%s)
        
        # Check for new crashes since last check
        while IFS= read -r -d '' crash_file; do
            local crash_time=$(stat -f "%Sm" -t "%s" "$crash_file" 2>/dev/null || echo "0")
            
            if [[ $crash_time -gt $last_check ]]; then
                local crash_date=$(date -r "$crash_time" '+%Y-%m-%d %H:%M:%S')
                log_error "NEW CRASH DETECTED: $(basename "$crash_file") at $crash_date"
                
                # Analyze immediately
                local analysis_file="$PROJECT_ROOT/test-results/crash-analysis-$(basename "$crash_file" .ips).txt"
                mkdir -p "$(dirname "$analysis_file")"
                analyze_crash_report "$crash_file" "$analysis_file"
                
                log_info "Analysis saved to: $analysis_file"
                
                # Show brief analysis
                echo ""
                head -30 "$analysis_file"
                echo ""
            fi
        done < <(find "$CRASH_REPORTS_DIR" -name "gecko-*.ips" -print0 2>/dev/null || true)
        
        last_check=$current_time
    done
}

# Clean up old crash reports
cleanup_crashes() {
    local days_old="${1:-7}"
    log_info "Cleaning up crash reports older than $days_old days..."
    
    local current_time=$(date +%s)
    local cutoff_time=$((current_time - days_old * 24 * 3600))
    local cleaned_count=0
    
    while IFS= read -r -d '' crash_file; do
        local crash_time=$(stat -f "%Sm" -t "%s" "$crash_file" 2>/dev/null || echo "0")
        
        if [[ $crash_time -lt $cutoff_time ]]; then
            log_info "Removing old crash report: $(basename "$crash_file")"
            rm "$crash_file"
            ((cleaned_count++))
        fi
    done < <(find "$CRASH_REPORTS_DIR" -name "gecko-*.ips" -print0 2>/dev/null || true)
    
    log_success "Cleaned up $cleaned_count old crash reports"
}

# Show usage
show_usage() {
    echo "Crash Detection Utility for Gecko"
    echo ""
    echo "Usage: $0 [command] [options]"
    echo ""
    echo "Commands:"
    echo "  find [hours]     Find recent crashes (default: 24 hours)"
    echo "  monitor          Monitor for new crashes in real-time"
    echo "  cleanup [days]   Clean up crash reports older than N days (default: 7)"
    echo "  analyze <file>   Analyze a specific crash report"
    echo ""
    echo "Examples:"
    echo "  $0 find 48       # Find crashes in last 48 hours"
    echo "  $0 monitor       # Monitor for new crashes"
    echo "  $0 cleanup 14    # Remove crashes older than 14 days"
    echo "  $0 analyze /path/to/crash.ips  # Analyze specific crash"
}

# Main function
main() {
    case "${1:-find}" in
        "find")
            find_recent_crashes "${2:-24}"
            ;;
        "monitor")
            monitor_crashes
            ;;
        "cleanup")
            cleanup_crashes "${2:-7}"
            ;;
        "analyze")
            if [[ -n "${2:-}" && -f "$2" ]]; then
                local output_file="$PROJECT_ROOT/test-results/manual-crash-analysis.txt"
                mkdir -p "$(dirname "$output_file")"
                analyze_crash_report "$2" "$output_file"
                log_success "Analysis saved to: $output_file"
                cat "$output_file"
            else
                log_error "Please provide a valid crash report file"
                exit 1
            fi
            ;;
        "help"|"-h"|"--help")
            show_usage
            ;;
        *)
            log_error "Unknown command: $1"
            show_usage
            exit 1
            ;;
    esac
}

# Handle interruption
trap 'log_warning "Monitoring stopped"; exit 0' INT TERM

main "$@"