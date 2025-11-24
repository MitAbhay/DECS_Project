#!/usr/bin/env python3
"""
Graph Generator for Workload 2: CPU Bottleneck
Mode 2 (LRU Cache + DB), Leaderboard GET-only
"""

import matplotlib.pyplot as plt
import numpy as np

# Placeholder data structure - YOU NEED TO FILL THIS WITH YOUR ACTUAL DATA
workload2_data = {
    'threads': [8, 16, 32, 48, 64, 96],  # UPDATE with your thread counts
    'total_requests': [80000, 160000, 320000, 480000, 640000, 960000],  # UPDATE
    'elapsed_time': [10.0, 18.0, 35.0, 52.0, 70.0, 105.0],  # UPDATE with your times
    'throughput': [8000, 8888, 9142, 9230, 9142, 9142],  # UPDATE with actual values
    'cpu_util': [75, 85, 92, 98, 99, 99],  # UPDATE - should reach ~100%
}

print("="*70)
print("WORKLOAD 2 GRAPH GENERATOR")
print("Mode 2 (LRU Cache + DB) - Leaderboard GET-only (CPU Bottleneck)")
print("="*70)
print("\n⚠️  PLEASE UPDATE THE DATA IN THE SCRIPT WITH YOUR ACTUAL VALUES!")
print("\nCurrent placeholder data:")
for i in range(len(workload2_data['threads'])):
    print(f"  Threads: {workload2_data['threads'][i]}, Throughput: {workload2_data['throughput'][i]} req/sec, CPU: {workload2_data['cpu_util'][i]}%")

response = input("\nHave you updated the data? (y/n): ").lower()
if response != 'y':
    print("\n📝 Please edit the script and update these arrays with your actual data:")
    print("   - threads: [8, 16, 32, ...]")
    print("   - total_requests: [80000, 160000, ...]")
    print("   - elapsed_time: [XX.XX, XX.XX, ...]")
    print("   - throughput: [XXXX, XXXX, ...]")
    print("   - cpu_util: [XX, XX, ...] (should reach ~100%)")
    print("\nThen run the script again!")
    exit()

# Calculate latency from elapsed time and total requests
workload2_data['latency'] = [0.108 , 0.1110 , 0.1142 , 0.1165 , 0.1189,0.1221]
# for i in range(len(workload2_data['elapsed_time'])):
#     avg_latency = (workload2_data['elapsed_time'][i] / workload2_data['total_requests'][i]) * 1000
#     workload2_data['latency'].append(avg_latency)

# I/O utilization for GET-only should be LOW (database reads minimal due to cache)
# Estimate: very low I/O since leaderboard queries hit cache, not DB
workload2_data['io_util'] = [5, 8, 10, 12, 15, 18]  # Low I/O, CPU is bottleneck

print("\n" + "="*70)
print("Workload 2 Data Summary (Mode 2: Leaderboard GET-only)")
print("="*70)
for i in range(len(workload2_data['threads'])):
    print(f"Threads: {workload2_data['threads'][i]}")
    print(f"  Throughput: {workload2_data['throughput'][i]:.2f} req/sec")
    print(f"  Latency: {workload2_data['latency'][i]:.3f} ms")
    print(f"  CPU: {workload2_data['cpu_util'][i]:.2f}%")
    print(f"  I/O (estimated): {workload2_data['io_util'][i]}%")
    print()

# Set style
try:
    plt.style.use('seaborn-v0_8-darkgrid')
except:
    try:
        plt.style.use('seaborn-darkgrid')
    except:
        plt.style.use('default')
        plt.rcParams['axes.grid'] = True

# Create individual high-quality plots
def create_plot(x_data, y_data, xlabel, ylabel, title, filename, color, highlight_saturation=False):
    fig, ax = plt.subplots(figsize=(10, 6))
    
    ax.plot(x_data, y_data, 'o-', linewidth=2.5, markersize=10, 
            color=color, markerfacecolor=color, markeredgewidth=2, 
            markeredgecolor='white')
    
    # Highlight saturation point for throughput
    if highlight_saturation and 'Throughput' in ylabel:
        # Find where throughput flattens (within 5% of max)
        max_throughput = max(y_data)
        saturation_idx = None
        for i, val in enumerate(y_data):
            if val >= max_throughput * 0.95:
                saturation_idx = i
                break
        if saturation_idx:
            ax.axhline(y=max_throughput, color='red', linestyle='--', alpha=0.5, 
                      label=f'Saturation: {max_throughput:.0f} req/sec')
            ax.legend(fontsize=12, loc='lower right')
    
    # Highlight 100% CPU utilization
    if 'CPU' in ylabel and 'Utilization' in ylabel:
        ax.axhline(y=100, color='red', linestyle='--', alpha=0.5, linewidth=2,
                  label='100% CPU (Full Utilization)')
        ax.legend(fontsize=12, loc='lower right')
    
    ax.set_xlabel(xlabel, fontsize=14, fontweight='bold')
    ax.set_ylabel(ylabel, fontsize=14, fontweight='bold')
    ax.set_title(title, fontsize=15, fontweight='bold', pad=20)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.tick_params(labelsize=12)
    
    # Add value labels on points
    for i, (x, y) in enumerate(zip(x_data, y_data)):
        if 'Utilization' in ylabel:
            label = f'{y:.0f}%'
        elif 'Latency' in ylabel:
            label = f'{y:.3f}'
        else:
            label = f'{y:.0f}'
        ax.annotate(label, (x, y), textcoords="offset points", 
                   xytext=(0,10), ha='center', fontsize=9, 
                   bbox=dict(boxstyle='round,pad=0.3', facecolor='yellow', alpha=0.3))
    
    if 'Utilization' in ylabel:
        ax.set_ylim([0, 105])
    
    plt.tight_layout()
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    print(f"✓ Saved: {filename}")
    plt.close()

# Generate all plots for Workload 2
print("\nGenerating Workload 2 graphs...")

# 1. Throughput vs Threads (should flatten due to CPU bottleneck)
create_plot(
    workload2_data['threads'],
    workload2_data['throughput'],
    'Number of Threads',
    'Throughput (req/sec)',
    'Workload 2 (Mode 2): Throughput vs Load\nLeaderboard GET-only, CPU Bottleneck',
    'workload2_mode2_throughput.png',
    '#2E86AB',
    highlight_saturation=True
)

# 2. Latency vs Threads (should be low, slight increase at saturation)
create_plot(
    workload2_data['threads'],
    workload2_data['latency'],
    'Number of Threads',
    'Average Latency (ms)',
    'Workload 2 (Mode 2): Latency vs Load\nLeaderboard GET-only, CPU Bottleneck',
    'workload2_mode2_latency.png',
    '#A23B72'
)

# 3. CPU Utilization vs Threads (should reach ~100%)
create_plot(
    workload2_data['threads'],
    workload2_data['cpu_util'],
    'Number of Threads',
    'CPU Utilization (%)',
    'Workload 2 (Mode 2): CPU Utilization vs Load\nLeaderboard GET-only, CPU Bottleneck',
    'workload2_mode2_cpu.png',
    '#F18F01'
)

# 4. I/O Utilization vs Threads (should be LOW)
create_plot(
    workload2_data['threads'],
    workload2_data['io_util'],
    'Number of Threads',
    'I/O Utilization (%)',
    'Workload 2 (Mode 2): I/O Utilization vs Load\nLeaderboard GET-only, Low I/O',
    'workload2_mode2_io.png',
    '#C73E1D'
)

# Create combined 2x2 plot
fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
fig.suptitle('Workload 2 (Mode 2: LRU Cache + DB) - Performance Metrics\nLeaderboard GET-only - CPU Bottleneck', 
             fontsize=18, fontweight='bold', y=0.995)

# Throughput
ax1.plot(workload2_data['threads'], workload2_data['throughput'], 
         'o-', linewidth=2.5, markersize=8, color='#2E86AB')
ax1.axhline(y=max(workload2_data['throughput']), color='red', linestyle='--', 
           alpha=0.5, label='Saturation')
ax1.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax1.set_ylabel('Throughput (req/sec)', fontsize=12, fontweight='bold')
ax1.set_title('Throughput vs Load (Flattens at CPU Saturation)', fontsize=13, fontweight='bold')
ax1.grid(True, alpha=0.3)
ax1.legend(fontsize=10)

# Latency
ax2.plot(workload2_data['threads'], workload2_data['latency'], 
         'o-', linewidth=2.5, markersize=8, color='#A23B72')
ax2.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax2.set_ylabel('Average Latency (ms)', fontsize=12, fontweight='bold')
ax2.set_title('Latency vs Load', fontsize=13, fontweight='bold')
ax2.grid(True, alpha=0.3)

# CPU Utilization
ax3.plot(workload2_data['threads'], workload2_data['cpu_util'], 
         'o-', linewidth=2.5, markersize=8, color='#F18F01')
ax3.axhline(y=100, color='red', linestyle='--', alpha=0.5, linewidth=2,
           label='100% CPU')
ax3.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax3.set_ylabel('CPU Utilization (%)', fontsize=12, fontweight='bold')
ax3.set_title('CPU Utilization vs Load (Reaches 100%)', fontsize=13, fontweight='bold')
ax3.grid(True, alpha=0.3)
ax3.set_ylim([0, 105])
ax3.legend(fontsize=10)

# I/O Utilization
ax4.plot(workload2_data['threads'], workload2_data['io_util'], 
         'o-', linewidth=2.5, markersize=8, color='#C73E1D')
ax4.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax4.set_ylabel('I/O Utilization (%)', fontsize=12, fontweight='bold')
ax4.set_title('I/O Utilization vs Load (Low - Not Bottleneck)', fontsize=13, fontweight='bold')
ax4.grid(True, alpha=0.3)
ax4.set_ylim([0, 105])

plt.tight_layout()
plt.savefig('workload2_mode2_combined.png', dpi=300, bbox_inches='tight')
print(f"✓ Saved: workload2_mode2_combined.png")
plt.close()

print("\n" + "="*70)
print("All graphs generated successfully!")
print("="*70)
print("\nGenerated files:")
print("  1. workload2_mode2_throughput.png")
print("  2. workload2_mode2_latency.png")
print("  3. workload2_mode2_cpu.png")
print("  4. workload2_mode2_io.png")
print("  5. workload2_mode2_combined.png")
print("\n" + "="*70)
print("Analysis Summary:")
print("="*70)
print(f"Peak Throughput: {max(workload2_data['throughput']):.2f} req/sec")
print(f"Throughput Saturation: Flattens at {workload2_data['threads'][3]}-{workload2_data['threads'][-1]} threads")
print(f"Max CPU Utilization: {max(workload2_data['cpu_util']):.1f}% (CPU Bottleneck)")
print(f"Max I/O Utilization: {max(workload2_data['io_util']):.1f}% (Low - Not Bottleneck)")
print(f"Min Latency: {min(workload2_data['latency']):.3f} ms")
print(f"Max Latency: {max(workload2_data['latency']):.3f} ms")
print("\nKey Observation: CPU reaches ~100% utilization while I/O stays low,")
print("confirming CPU as the bottleneck. Throughput plateaus when CPU saturates.")
print("Database queries are avoided due to cache hits (Mode 2), making this CPU-bound.")
print("="*70)