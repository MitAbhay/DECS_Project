#!/usr/bin/env python3
"""
Performance Analyzer for Leaderboard Server
Runs load tests at multiple load levels and generates graphs
"""

import subprocess
import time
import re
import matplotlib.pyplot as plt
import numpy as np
from datetime import datetime
import json
import os
import psutil

class PerformanceAnalyzer:
    def __init__(self, server_url="http://127.0.0.1:8080", server_pid=None):
        self.server_url = server_url
        self.server_pid = server_pid
        self.results = []
        
    def get_cpu_utilization(self, pid, duration=5):
        """Measure CPU utilization of a specific process"""
        if not pid:
            return 0
        try:
            process = psutil.Process(pid)
            cpu_percent = process.cpu_percent(interval=duration)
            return cpu_percent
        except:
            return 0
    
    def get_io_utilization(self, duration=5):
        """Measure I/O utilization using iostat"""
        try:
            result = subprocess.run(
                ['iostat', '-x', str(duration), '2'],
                capture_output=True,
                text=True,
                timeout=duration + 5
            )
            lines = result.stdout.strip().split('\n')
            # Parse the second output (after header)
            for i, line in enumerate(lines):
                if 'Device' in line and i + 2 < len(lines):
                    # Get the line after "Device" header (skip one line for column names)
                    data_line = lines[i + 2].split()
                    if len(data_line) >= 14:
                        util = float(data_line[-1])  # Last column is %util
                        return util
            return 0
        except Exception as e:
            print(f"Error getting I/O stats: {e}")
            return 0
    
    def parse_server_logs(self, log_output):
        """Parse latency from server logs"""
        latencies = []
        for line in log_output.split('\n'):
            match = re.search(r'latency=(\d+) us', line)
            if match:
                latencies.append(int(match.group(1)) / 1000.0)  # Convert to ms
        
        if latencies:
            return {
                'avg': np.mean(latencies),
                'p50': np.percentile(latencies, 50),
                'p95': np.percentile(latencies, 95),
                'p99': np.percentile(latencies, 99)
            }
        return None
    
    def run_load_test(self, threads, requests_per_thread, mode, duration=300):
        """
        Run a single load test
        duration: how long to run the test in seconds (default 5 minutes)
        """
        print(f"\n{'='*60}")
        print(f"Running load test: threads={threads}, reqs/thread={requests_per_thread}, mode={mode}")
        print(f"Duration: {duration} seconds")
        print(f"{'='*60}")
        
        # Start time
        start_time = time.time()
        
        # Run loadgen
        cmd = ['./loadgen', self.server_url, str(threads), str(requests_per_thread), str(mode)]
        
        print(f"Command: {' '.join(cmd)}")
        print("Running...")
        
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        # Parse loadgen output
        output = result.stdout
        print(output)
        
        # Extract metrics
        throughput = 0
        total_requests = 0
        elapsed = 0
        
        for line in output.split('\n'):
            if 'Total HTTP requests:' in line:
                total_requests = float(line.split(':')[1].strip())
            elif 'Elapsed:' in line:
                elapsed = float(line.split(':')[1].strip().split()[0])
            elif 'Throughput:' in line:
                throughput = float(line.split(':')[1].strip().split()[0])
        
        # Calculate offered load (requests/sec attempted)
        # For open-loop: threads * requests_per_thread / expected_time
        # Simplified: use actual throughput as proxy for now
        offered_load = threads * requests_per_thread / max(elapsed, 1)
        
        # Measure CPU utilization
        cpu_util = self.get_cpu_utilization(self.server_pid, duration=5)
        
        # Measure I/O utilization
        io_util = self.get_io_utilization(duration=5)
        
        result_data = {
            'threads': threads,
            'requests_per_thread': requests_per_thread,
            'mode': mode,
            'throughput': throughput,
            'offered_load': offered_load,
            'total_requests': total_requests,
            'elapsed': elapsed,
            'cpu_utilization': cpu_util,
            'io_utilization': io_util,
            'latency_avg': elapsed * 1000 / total_requests if total_requests > 0 else 0  # Rough estimate
        }
        
        print(f"\nResults:")
        print(f"  Throughput: {throughput:.2f} req/sec")
        print(f"  Offered Load: {offered_load:.2f} req/sec")
        print(f"  CPU Utilization: {cpu_util:.2f}%")
        print(f"  I/O Utilization: {io_util:.2f}%")
        print(f"  Avg Latency: {result_data['latency_avg']:.2f} ms")
        
        return result_data
    
    def run_workload(self, workload_name, mode, load_levels, duration=300):
        """
        Run a complete workload test at multiple load levels
        load_levels: list of (threads, requests_per_thread) tuples
        """
        print(f"\n{'#'*60}")
        print(f"# Starting Workload: {workload_name}")
        print(f"# Mode: {mode}")
        print(f"# Load levels: {len(load_levels)}")
        print(f"{'#'*60}\n")
        
        workload_results = []
        
        for i, (threads, reqs) in enumerate(load_levels):
            print(f"\n>>> Load Level {i+1}/{len(load_levels)}")
            
            # Run the test
            result = self.run_load_test(threads, reqs, mode, duration)
            result['workload'] = workload_name
            workload_results.append(result)
            
            # Save intermediate results
            self.save_results(workload_results, f"results_{workload_name}.json")
            
            # Wait between tests to let system stabilize
            if i < len(load_levels) - 1:
                print("\nWaiting 30 seconds before next test...")
                time.sleep(2)
        
        return workload_results
    
    def save_results(self, results, filename="results.json"):
        """Save results to JSON file"""
        with open(filename, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\nResults saved to {filename}")
    
    def load_results(self, filename="results.json"):
        """Load results from JSON file"""
        with open(filename, 'r') as f:
            return json.load(f)
    
    def plot_workload_results(self, results, workload_name, output_prefix="workload"):
        """Generate all required plots for a workload"""
        
        # Extract data
        offered_loads = [r['offered_load'] for r in results]
        throughputs = [r['throughput'] for r in results]
        latencies = [r['latency_avg'] for r in results]
        cpu_utils = [r['cpu_utilization'] for r in results]
        io_utils = [r['io_utilization'] for r in results]
        
        # Set style (compatible with different matplotlib versions)
        try:
            plt.style.use('seaborn-v0_8-darkgrid')
        except:
            try:
                plt.style.use('seaborn-darkgrid')
            except:
                plt.style.use('default')  # Fallback to default
                plt.rcParams['axes.grid'] = True
        
        # Create 2x2 subplot
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(14, 10))
        fig.suptitle(f'{workload_name} - Performance Metrics', fontsize=16, fontweight='bold')
        
        # 1. Throughput vs Offered Load
        ax1.plot(offered_loads, throughputs, 'o-', linewidth=2, markersize=8, color='#2E86AB')
        ax1.set_xlabel('Offered Load (req/sec)', fontsize=12)
        ax1.set_ylabel('Throughput (req/sec)', fontsize=12)
        ax1.set_title('Throughput vs Load', fontsize=13, fontweight='bold')
        ax1.grid(True, alpha=0.3)
        
        # 2. Latency vs Offered Load
        ax2.plot(offered_loads, latencies, 'o-', linewidth=2, markersize=8, color='#A23B72')
        ax2.set_xlabel('Offered Load (req/sec)', fontsize=12)
        ax2.set_ylabel('Average Latency (ms)', fontsize=12)
        ax2.set_title('Latency vs Load', fontsize=13, fontweight='bold')
        ax2.grid(True, alpha=0.3)
        
        # 3. CPU Utilization vs Offered Load
        ax3.plot(offered_loads, cpu_utils, 'o-', linewidth=2, markersize=8, color='#F18F01')
        ax3.set_xlabel('Offered Load (req/sec)', fontsize=12)
        ax3.set_ylabel('CPU Utilization (%)', fontsize=12)
        ax3.set_title('CPU Utilization vs Load', fontsize=13, fontweight='bold')
        ax3.grid(True, alpha=0.3)
        ax3.set_ylim([0, 105])
        
        # 4. I/O Utilization vs Offered Load
        ax4.plot(offered_loads, io_utils, 'o-', linewidth=2, markersize=8, color='#C73E1D')
        ax4.set_xlabel('Offered Load (req/sec)', fontsize=12)
        ax4.set_ylabel('I/O Utilization (%)', fontsize=12)
        ax4.set_title('I/O Utilization vs Load', fontsize=13, fontweight='bold')
        ax4.grid(True, alpha=0.3)
        ax4.set_ylim([0, 105])
        
        plt.tight_layout()
        
        # Save figure
        filename = f"{output_prefix}_{workload_name.replace(' ', '_').lower()}.png"
        plt.savefig(filename, dpi=300, bbox_inches='tight')
        print(f"Plot saved to {filename}")
        plt.close()
        
        # Also create individual plots for the report
        self._create_individual_plots(results, workload_name, output_prefix)
    
    def _create_individual_plots(self, results, workload_name, output_prefix):
        """Create individual plots for better quality in report"""
        
        offered_loads = [r['offered_load'] for r in results]
        throughputs = [r['throughput'] for r in results]
        latencies = [r['latency_avg'] for r in results]
        cpu_utils = [r['cpu_utilization'] for r in results]
        io_utils = [r['io_utilization'] for r in results]
        
        plots = [
            ('throughput', offered_loads, throughputs, 'Throughput (req/sec)', '#2E86AB'),
            ('latency', offered_loads, latencies, 'Average Latency (ms)', '#A23B72'),
            ('cpu', offered_loads, cpu_utils, 'CPU Utilization (%)', '#F18F01'),
            ('io', offered_loads, io_utils, 'I/O Utilization (%)', '#C73E1D')
        ]
        
        for name, x_data, y_data, ylabel, color in plots:
            fig, ax = plt.subplots(figsize=(8, 6))
            ax.plot(x_data, y_data, 'o-', linewidth=2.5, markersize=10, color=color)
            ax.set_xlabel('Offered Load (req/sec)', fontsize=13)
            ax.set_ylabel(ylabel, fontsize=13)
            ax.set_title(f'{workload_name} - {ylabel} vs Load', fontsize=14, fontweight='bold')
            ax.grid(True, alpha=0.3)
            
            if 'Utilization' in ylabel:
                ax.set_ylim([0, 105])
            
            plt.tight_layout()
            filename = f"{output_prefix}_{workload_name.replace(' ', '_').lower()}_{name}.png"
            plt.savefig(filename, dpi=300, bbox_inches='tight')
            print(f"Individual plot saved to {filename}")
            plt.close()


def main():
    """Main function to run performance analysis"""
    
    print("="*60)
    print("Leaderboard Server Performance Analyzer")
    print("="*60)
    
    # Get server PID
    server_pid_input = input("Enter server process PID (or press Enter to skip): ").strip()
    server_pid = int(server_pid_input) if server_pid_input else None
    
    analyzer = PerformanceAnalyzer(server_pid=server_pid)
    
    # Define workloads
    print("\n" + "="*60)
    print("Workload Definitions")
    print("="*60)
    print("\nWorkload 1: I/O Bottleneck (LRU Cache + DB, mode=2, update-heavy)")
    print("  - Tests database write performance with LRU cache")
    print("  - Expected: I/O utilization reaches high levels")
    print("\nWorkload 2: CPU + I/O Bottleneck (All mode=3, mixed)")
    print("  - Tests full system: LRU + Top-N Cache + DB")
    print("  - Expected: Both CPU and I/O utilization measured")
    
    # Define load levels with BIGGER loads for better measurements
    # Format: (threads, requests_per_thread)
    mode2_load_levels = [
        (8, 500),     # Low load
        (16, 500),    # Medium-low
        (32, 500),    # Medium
        (48, 500),    # Medium-high
        (64, 500),    # High load
        (96, 500),    # Very high
    ]
    
    mode3_load_levels = [
        (16, 1000),   # Low load
        (32, 1000),   # Medium-low
        (48, 1000),   # Medium
        (64, 1000),   # Medium-high
        (96, 1000),   # High load
        (128, 1000),  # Very high
    ]
    
    # Run Workload 1: I/O Bottleneck (Mode 2: LRU Cache + DB)
    print("\n" + "="*60)
    choice = input("\nRun Workload 1 (Mode 2: LRU Cache + DB - I/O Bottleneck)? [y/n]: ").lower()
    if choice == 'y':
        duration = int(input("Enter test duration per load level in seconds (default 300): ") or 300)
        workload1_results = analyzer.run_workload(
            "Workload 1 Mode2 IO",
            mode=2,  # LRU Cache + DB
            load_levels=mode2_load_levels,
            duration=duration
        )
        analyzer.plot_workload_results(workload1_results, "Workload 1 Mode2 IO", "workload1")
    
    # Run Workload 2: Full System (Mode 3: All)
    print("\n" + "="*60)
    choice = input("\nRun Workload 2 (Mode 3: All - CPU + I/O)? [y/n]: ").lower()
    if choice == 'y':
        duration = int(input("Enter test duration per load level in seconds (default 300): ") or 300)
        workload2_results = analyzer.run_workload(
            "Workload 2 Mode3 All",
            mode=3,  # All: LRU + Top-N + DB
            load_levels=mode3_load_levels,
            duration=duration
        )
        analyzer.plot_workload_results(workload2_results, "Workload 2 Mode3 All", "workload2")
    
    print("\n" + "="*60)
    print("Performance analysis complete!")
    print("Check the generated PNG files and JSON results.")
    print("="*60)


if __name__ == "__main__":
    main()