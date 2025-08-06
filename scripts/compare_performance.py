#!/usr/bin/env python3
"""
Compare performance between two benchmark runs or branches.
Useful for local development to check performance impact of changes.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from typing import Dict, Optional


def run_benchmarks(build_dir: str, samples: int = 50) -> str:
    """Run benchmarks and return path to XML output."""
    with tempfile.NamedTemporaryFile(mode='w', suffix='.xml', delete=False) as f:
        xml_output = f.name
    
    perf_test = os.path.join(build_dir, 'performance_tests')
    if not os.path.exists(perf_test):
        # Try alternative locations
        for alt in ['tests/performance_tests', 'bin/performance_tests']:
            alt_path = os.path.join(build_dir, alt)
            if os.path.exists(alt_path):
                perf_test = alt_path
                break
    
    if not os.path.exists(perf_test):
        raise FileNotFoundError(f"Performance test executable not found in {build_dir}")
    
    cmd = [
        perf_test,
        '[performance]',
        '--benchmark-samples', str(samples),
        '--reporter', 'xml',
        '--out', xml_output
    ]
    
    print(f"Running benchmarks: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"Error running benchmarks: {result.stderr}")
        raise RuntimeError("Benchmark execution failed")
    
    return xml_output


def load_json_results(json_file: str) -> Dict:
    """Load benchmark results from JSON file."""
    with open(json_file, 'r') as f:
        return json.load(f)


def compare_branches(branch1: str, branch2: str, samples: int = 50) -> Dict:
    """Compare performance between two git branches."""
    # Get current branch
    current_branch = subprocess.check_output(
        ['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
        text=True
    ).strip()
    
    results = {}
    
    try:
        # Test branch1
        print(f"\n=== Checking out branch: {branch1} ===")
        subprocess.run(['git', 'checkout', branch1], check=True)
        subprocess.run(['make', '-j4'], check=True)
        
        xml1 = run_benchmarks('build', samples)
        
        # Parse results
        track_script = os.path.join(os.path.dirname(__file__), 'track_performance.py')
        json1 = xml1.replace('.xml', '.json')
        subprocess.run([
            sys.executable, track_script,
            xml1,
            '--json-output', json1
        ], check=True)
        
        results['branch1'] = {
            'name': branch1,
            'data': load_json_results(json1)
        }
        
        # Test branch2
        print(f"\n=== Checking out branch: {branch2} ===")
        subprocess.run(['git', 'checkout', branch2], check=True)
        subprocess.run(['make', '-j4'], check=True)
        
        xml2 = run_benchmarks('build', samples)
        
        json2 = xml2.replace('.xml', '.json')
        subprocess.run([
            sys.executable, track_script,
            xml2,
            '--json-output', json2
        ], check=True)
        
        results['branch2'] = {
            'name': branch2,
            'data': load_json_results(json2)
        }
        
    finally:
        # Restore original branch
        print(f"\n=== Restoring branch: {current_branch} ===")
        subprocess.run(['git', 'checkout', current_branch])
    
    return results


def generate_comparison_report(results: Dict, threshold: float = 0.05) -> str:
    """Generate comparison report between two sets of results."""
    report = []
    
    if 'branch1' in results and 'branch2' in results:
        report.append(f"# Performance Comparison: {results['branch1']['name']} vs {results['branch2']['name']}")
    else:
        report.append("# Performance Comparison")
    
    report.append("")
    
    # Extract benchmark data
    data1 = results.get('branch1', results.get('before', {})).get('data', {})
    data2 = results.get('branch2', results.get('after', {})).get('data', {})
    
    if 'comparison' in data1:
        benchmarks1 = {k: v['current_data'] for k, v in data1['comparison'].items()}
    else:
        benchmarks1 = data1.get('benchmarks', {})
    
    if 'comparison' in data2:
        benchmarks2 = {k: v['current_data'] for k, v in data2['comparison'].items()}
    else:
        benchmarks2 = data2.get('benchmarks', {})
    
    # Compare benchmarks
    all_benchmarks = set(benchmarks1.keys()) | set(benchmarks2.keys())
    
    improvements = []
    regressions = []
    neutral = []
    
    for bench_name in sorted(all_benchmarks):
        if bench_name in benchmarks1 and bench_name in benchmarks2:
            mean1 = benchmarks1[bench_name]['mean']
            mean2 = benchmarks2[bench_name]['mean']
            
            change_ratio = (mean2 - mean1) / mean1 if mean1 > 0 else 0
            change_percent = change_ratio * 100
            
            result = {
                'name': bench_name,
                'mean1': mean1,
                'mean2': mean2,
                'change_percent': change_percent
            }
            
            if change_ratio > threshold:
                regressions.append(result)
            elif change_ratio < -threshold:
                improvements.append(result)
            else:
                neutral.append(result)
    
    # Summary
    report.append("## Summary")
    report.append(f"- Total benchmarks: {len(all_benchmarks)}")
    report.append(f"- Improvements: {len(improvements)}")
    report.append(f"- Regressions: {len(regressions)}")
    report.append(f"- No significant change: {len(neutral)}")
    report.append("")
    
    # Detailed results
    if regressions:
        report.append("## 🔴 Performance Regressions")
        for r in sorted(regressions, key=lambda x: x['change_percent'], reverse=True):
            report.append(f"- **{r['name']}**: {r['change_percent']:.1f}% slower")
            report.append(f"  - Before: {r['mean1']:.2f} ns")
            report.append(f"  - After: {r['mean2']:.2f} ns")
        report.append("")
    
    if improvements:
        report.append("## 🟢 Performance Improvements")
        for r in sorted(improvements, key=lambda x: x['change_percent']):
            report.append(f"- **{r['name']}**: {abs(r['change_percent']):.1f}% faster")
            report.append(f"  - Before: {r['mean1']:.2f} ns")
            report.append(f"  - After: {r['mean2']:.2f} ns")
        report.append("")
    
    return "\n".join(report)


def main():
    parser = argparse.ArgumentParser(
        description='Compare performance between branches or benchmark runs'
    )
    
    subparsers = parser.add_subparsers(dest='command', help='Command to run')
    
    # Branch comparison
    branch_parser = subparsers.add_parser('branches', help='Compare two git branches')
    branch_parser.add_argument('branch1', help='First branch to compare')
    branch_parser.add_argument('branch2', help='Second branch to compare')
    branch_parser.add_argument('--samples', type=int, default=50,
                              help='Number of benchmark samples')
    branch_parser.add_argument('--threshold', type=float, default=0.05,
                              help='Significance threshold (default: 5%%)')
    
    # File comparison
    file_parser = subparsers.add_parser('files', help='Compare two benchmark result files')
    file_parser.add_argument('file1', help='First result file (JSON)')
    file_parser.add_argument('file2', help='Second result file (JSON)')
    file_parser.add_argument('--threshold', type=float, default=0.05,
                            help='Significance threshold (default: 5%%)')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
    
    results = {}
    
    if args.command == 'branches':
        results = compare_branches(args.branch1, args.branch2, args.samples)
    elif args.command == 'files':
        results = {
            'before': {'name': args.file1, 'data': load_json_results(args.file1)},
            'after': {'name': args.file2, 'data': load_json_results(args.file2)}
        }
    
    # Generate report
    report = generate_comparison_report(results, args.threshold)
    print("\n" + report)
    
    return 0


if __name__ == '__main__':
    sys.exit(main())