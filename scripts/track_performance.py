#!/usr/bin/env python3
"""
Performance tracking script for Catch2 benchmarks.
Parses XML output and tracks performance over time.
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
from datetime import datetime
from typing import Dict, List, Optional, Tuple
import subprocess
import platform


class PerformanceTracker:
    def __init__(self, baseline_file: str = "tests/performance/baseline.json"):
        self.baseline_file = baseline_file
        self.baseline_data = self.load_baseline()
        
    def load_baseline(self) -> Dict:
        """Load baseline performance data if it exists."""
        if os.path.exists(self.baseline_file):
            with open(self.baseline_file, 'r') as f:
                return json.load(f)
        return {
            "benchmarks": {},
            "metadata": {
                "version": "1.0",
                "created": datetime.now().isoformat()
            }
        }
    
    def save_baseline(self):
        """Save baseline data to file."""
        os.makedirs(os.path.dirname(self.baseline_file), exist_ok=True)
        with open(self.baseline_file, 'w') as f:
            json.dump(self.baseline_data, f, indent=2)
    
    def parse_catch2_xml(self, xml_file: str) -> Dict[str, Dict]:
        """Parse Catch2 XML output and extract benchmark results."""
        tree = ET.parse(xml_file)
        root = tree.getroot()
        
        results = {}
        
        for test_case in root.findall('.//TestCase'):
            test_name = test_case.get('name')
            
            for section in test_case.findall('.//Section'):
                section_name = section.get('name')
                
                for benchmark in section.findall('.//BenchmarkResults'):
                    bench_name = benchmark.get('name')
                    full_name = f"{test_name}/{section_name}/{bench_name}"
                    
                    mean_elem = benchmark.find('mean')
                    std_elem = benchmark.find('standardDeviation')
                    
                    results[full_name] = {
                        'mean': float(mean_elem.get('value')),
                        'mean_lower': float(mean_elem.get('lowerBound')),
                        'mean_upper': float(mean_elem.get('upperBound')),
                        'std_dev': float(std_elem.get('value')),
                        'samples': int(benchmark.get('samples')),
                        'iterations': int(benchmark.get('iterations')),
                        'timestamp': datetime.now().isoformat()
                    }
        
        return results
    
    def compare_results(self, current: Dict[str, Dict], threshold: float = 0.05) -> Dict[str, Dict]:
        """Compare current results with baseline and detect regressions."""
        comparison = {}
        
        for bench_name, current_data in current.items():
            if bench_name in self.baseline_data.get('benchmarks', {}):
                baseline = self.baseline_data['benchmarks'][bench_name]
                baseline_mean = baseline.get('mean', 0)
                current_mean = current_data['mean']
                
                if baseline_mean > 0:
                    change_ratio = (current_mean - baseline_mean) / baseline_mean
                    
                    status = "NEUTRAL"
                    if change_ratio > threshold:
                        status = "REGRESSION"
                    elif change_ratio < -threshold:
                        status = "IMPROVEMENT"
                    
                    comparison[bench_name] = {
                        'status': status,
                        'change_percent': change_ratio * 100,
                        'baseline_mean': baseline_mean,
                        'current_mean': current_mean,
                        'current_data': current_data
                    }
                else:
                    comparison[bench_name] = {
                        'status': 'NEW',
                        'current_mean': current_mean,
                        'current_data': current_data
                    }
            else:
                comparison[bench_name] = {
                    'status': 'NEW',
                    'current_mean': current_data['mean'],
                    'current_data': current_data
                }
        
        return comparison
    
    def update_baseline(self, results: Dict[str, Dict], force: bool = False):
        """Update baseline with new results."""
        if 'benchmarks' not in self.baseline_data:
            self.baseline_data['benchmarks'] = {}
        
        for bench_name, data in results.items():
            if force or bench_name not in self.baseline_data['benchmarks']:
                self.baseline_data['benchmarks'][bench_name] = data
        
        self.baseline_data['metadata']['last_updated'] = datetime.now().isoformat()
        self.save_baseline()
    
    def generate_report(self, comparison: Dict[str, Dict]) -> str:
        """Generate a human-readable performance report."""
        report = ["# Performance Report", ""]
        
        # Summary
        regressions = [k for k, v in comparison.items() if v['status'] == 'REGRESSION']
        improvements = [k for k, v in comparison.items() if v['status'] == 'IMPROVEMENT']
        new_benchmarks = [k for k, v in comparison.items() if v['status'] == 'NEW']
        
        report.append(f"## Summary")
        report.append(f"- Regressions: {len(regressions)}")
        report.append(f"- Improvements: {len(improvements)}")
        report.append(f"- New benchmarks: {len(new_benchmarks)}")
        report.append("")
        
        # Detailed results
        if regressions:
            report.append("## 🔴 Performance Regressions")
            for bench in regressions:
                data = comparison[bench]
                report.append(f"- **{bench}**: {data['change_percent']:.1f}% slower")
                report.append(f"  - Baseline: {data['baseline_mean']:.2f} ns")
                report.append(f"  - Current: {data['current_mean']:.2f} ns")
            report.append("")
        
        if improvements:
            report.append("## 🟢 Performance Improvements")
            for bench in improvements:
                data = comparison[bench]
                report.append(f"- **{bench}**: {abs(data['change_percent']):.1f}% faster")
                report.append(f"  - Baseline: {data['baseline_mean']:.2f} ns")
                report.append(f"  - Current: {data['current_mean']:.2f} ns")
            report.append("")
        
        if new_benchmarks:
            report.append("## 🆕 New Benchmarks")
            for bench in new_benchmarks:
                data = comparison[bench]
                report.append(f"- **{bench}**: {data['current_mean']:.2f} ns")
            report.append("")
        
        return "\n".join(report)
    
    def get_system_info(self) -> Dict:
        """Get system information for metadata."""
        return {
            'platform': platform.platform(),
            'processor': platform.processor(),
            'python_version': platform.python_version(),
            'timestamp': datetime.now().isoformat()
        }


def main():
    parser = argparse.ArgumentParser(description='Track Catch2 benchmark performance')
    parser.add_argument('xml_file', help='Catch2 XML output file')
    parser.add_argument('--baseline', default='tests/performance/baseline.json',
                        help='Baseline performance data file')
    parser.add_argument('--threshold', type=float, default=0.05,
                        help='Regression threshold (default: 5%%)')
    parser.add_argument('--update-baseline', action='store_true',
                        help='Update baseline with current results')
    parser.add_argument('--force-update', action='store_true',
                        help='Force update all baseline entries')
    parser.add_argument('--json-output', help='Output comparison results as JSON')
    parser.add_argument('--markdown-output', help='Output report as markdown file')
    
    args = parser.parse_args()
    
    # Initialize tracker
    tracker = PerformanceTracker(args.baseline)
    
    # Parse results
    try:
        results = tracker.parse_catch2_xml(args.xml_file)
    except Exception as e:
        print(f"Error parsing XML file: {e}", file=sys.stderr)
        return 1
    
    if not results:
        print("No benchmark results found in XML file", file=sys.stderr)
        return 1
    
    # Compare with baseline
    comparison = tracker.compare_results(results, args.threshold)
    
    # Generate report
    report = tracker.generate_report(comparison)
    print(report)
    
    # Save outputs if requested
    if args.json_output:
        with open(args.json_output, 'w') as f:
            json.dump({
                'comparison': comparison,
                'system_info': tracker.get_system_info()
            }, f, indent=2)
    
    if args.markdown_output:
        with open(args.markdown_output, 'w') as f:
            f.write(report)
    
    # Update baseline if requested
    if args.update_baseline or args.force_update:
        tracker.update_baseline(results, force=args.force_update)
        print(f"\nBaseline updated: {tracker.baseline_file}")
    
    # Return non-zero if regressions found
    regressions = [k for k, v in comparison.items() if v['status'] == 'REGRESSION']
    return 1 if regressions else 0


if __name__ == '__main__':
    sys.exit(main())