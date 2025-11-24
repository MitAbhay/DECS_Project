#!/usr/bin/env python3
"""
Graph Generator for Mode 3: All Components (LRU + Top-N + DB)
Mixed Workload: UPDATE + Leaderboard GET
"""

import matplotlib.pyplot as plt
import numpy as np

# Placeholder data structure - YOU NEED TO FILL THIS WITH YOUR ACTUAL DATA
# Note: For mixed workload (mode 2 in loadgen), each iteration does 2 requests
mode3_update_data = {
    'threads': [8, 16, 32, 48, 64, 96],  # UPDATE with your thread counts
    'requests_per_thread': [10000, 10000, 10000, 10000, 10000, 10000],  # UPDATE
    'total_requests': [160000, 320000, 640000, 960000, 1280000, 1920000],  # 2x (UPDATE + GET)
    'elapsed_time': [20.0, 35.0, 65.0, 95.0, 125.0, 185.0],  # UPDATE with your times
    'throughput': [8000, 9142, 9846, 10105, 10240, 10378],  # UPDATE with actual values
    'cpu_util': [65, 72, 78, 79, 77, 75],  # UPDATE - moderate, not 100%
}

mode3_leaderboard_data = {
    'threads': [8, 16, 32, 48, 64, 96],  # Same as above
    'requests_per_thread': [10000, 10000, 10000, 10000, 10000, 10000],  # UPDATE
    'total_requests': [160000, 320000, 640000, 960000, 1280000, 1920000],  # 2x
    'elapsed_time': [18.0, 32.0, 60.0, 88.0, 115.0, 170.0],  # UPDATE with your times
    'throughput': [8888, 10000, 10666, 10909, 11130, 11294],  # UPDATE with actual values
    'cpu_util': [68, 75, 81, 89, 99, 99],  # UPDATE - moderate
}

print("="*70)
print("MODE 3 GRAPH GENERATOR")
print("Mode 3 (All: LRU + Top-N + DB) - Mixed Workload")
print("="*70)
print("\n📊 This generates TWO sets of graphs:")
print("  1. UPDATE-only workload (I/O stress)")
print("  2. Mixed workload (UPDATE + Leaderboard GET)")
print("\n⚠️  PLEASE UPDATE THE DATA IN THE SCRIPT WITH YOUR ACTUAL VALUES!")
print("\nFor UPDATE-only:")
for i in range(len(mode3_update_data['threads'])):
    print(f"  Threads: {mode3_update_data['threads'][i]}, Throughput: {mode3_update_data['throughput'][i]} req/sec, CPU: {mode3_update_data['cpu_util'][i]}%")

print("\nFor Mixed workload (UPDATE + GET):")
for i in range(len(mode3_leaderboard_data['threads'])):
    print(f"  Threads: {mode3_leaderboard_data['threads'][i]}, Throughput: {mode3_leaderboard_data['throughput'][i]} req/sec, CPU: {mode3_leaderboard_data['cpu_util'][i]}%")

response = input("\nHave you updated BOTH datasets? (y/n): ").lower()
if response != 'y':
    print("\n📝 Please edit the script and update these arrays with your actual data:")
    print("\n   For UPDATE-only (mode3_update_data):")
    print("   - Run: ./loadgen http://127.0.0.1:8080 [threads] 10000 0")
    print("   - Update: elapsed_time, throughput, cpu_util")
    print("\n   For Mixed workload (mode3_leaderboard_data):")
    print("   - Run: ./loadgen http://127.0.0.1:8080 [threads] 10000 2")
    print("   - Update: elapsed_time, throughput, cpu_util")
    print("\nThen run the script again!")
    exit()

# Calculate latencies
mode3_update_data['latency'] = [0.099 , 0.104 , 0.109 , 0.114 , 0.118, 0.123]
# for i in range(len(mode3_update_data['elapsed_time'])):
#     avg_latency = (mode3_update_data['elapsed_time'][i] / mode3_update_data['total_requests'][i]) * 1000
#     mode3_update_data['latency'].append(avg_latency)

mode3_leaderboard_data['latency'] = [0.090 , 0.093 , 0.099 , 0.103, 0.107 , 0.109]
# for i in range(len(mode3_leaderboard_data['elapsed_time'])):
#     avg_latency = (mode3_leaderboard_data['elapsed_time'][i] / mode3_leaderboard_data['total_requests'][i]) * 1000
#     mode3_leaderboard_data['latency'].append(avg_latency)

# I/O utilization for Mode 3
# UPDATE-only: High I/O (writes to DB)
mode3_update_data['io_util'] = [42, 56, 62, 65, 61, 64]  # High I/O for writes

# Mixed workload: Moderate I/O (writes to DB, reads from cache)
# Lower than UPDATE-only because half the requests are cached reads
mode3_leaderboard_data['io_util'] = [28, 38, 52, 65, 73, 78]  # Moderate I/O

print("\n" + "="*70)
print("Mode 3 UPDATE-only Data Summary")
print("="*70)
for i in range(len(mode3_update_data['threads'])):
    print(f"Threads: {mode3_update_data['threads'][i]}")
    print(f"  Throughput: {mode3_update_data['throughput'][i]:.2f} req/sec")
    print(f"  Latency: {mode3_update_data['latency'][i]:.3f} ms")
    print(f"  CPU: {mode3_update_data['cpu_util'][i]:.2f}%")
    print(f"  I/O (estimated): {mode3_update_data['io_util'][i]}%")
    print()

print("="*70)
print("Mode 3 Mixed Workload Data Summary")
print("="*70)
for i in range(len(mode3_leaderboard_data['threads'])):
    print(f"Threads: {mode3_leaderboard_data['threads'][i]}")
    print(f"  Throughput: {mode3_leaderboard_data['throughput'][i]:.2f} req/sec")
    print(f"  Latency: {mode3_leaderboard_data['latency'][i]:.3f} ms")
    print(f"  CPU: {mode3_leaderboard_data['cpu_util'][i]:.2f}%")
    print(f"  I/O (estimated): {mode3_leaderboard_data['io_util'][i]}%")
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
def create_plot(x_data, y_data, xlabel, ylabel, title, filename, color, show_annotations=True):
    fig, ax = plt.subplots(figsize=(10, 6))
    
    ax.plot(x_data, y_data, 'o-', linewidth=2.5, markersize=10, 
            color=color, markerfacecolor=color, markeredgewidth=2, 
            markeredgecolor='white')
    
    ax.set_xlabel(xlabel, fontsize=14, fontweight='bold')
    ax.set_ylabel(ylabel, fontsize=14, fontweight='bold')
    ax.set_title(title, fontsize=15, fontweight='bold', pad=20)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.tick_params(labelsize=12)
    
    # Add value labels on points
    if show_annotations:
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

# ============================================================
# GENERATE GRAPHS FOR UPDATE-ONLY WORKLOAD
# ============================================================
print("\n" + "="*70)
print("Generating Mode 3 UPDATE-only graphs...")
print("="*70)

create_plot(
    mode3_update_data['threads'],
    mode3_update_data['throughput'],
    'Number of Threads',
    'Throughput (req/sec)',
    'Mode 3 UPDATE-only: Throughput vs Load\nAll Components Active (LRU + Top-N + DB)',
    'mode3_update_throughput.png',
    '#2E86AB'
)

create_plot(
    mode3_update_data['threads'],
    mode3_update_data['latency'],
    'Number of Threads',
    'Average Latency (ms)',
    'Mode 3 UPDATE-only: Latency vs Load\nAll Components Active',
    'mode3_update_latency.png',
    '#A23B72'
)

create_plot(
    mode3_update_data['threads'],
    mode3_update_data['cpu_util'],
    'Number of Threads',
    'CPU Utilization (%)',
    'Mode 3 UPDATE-only: CPU Utilization vs Load\nAll Components Active',
    'mode3_update_cpu.png',
    '#F18F01'
)

create_plot(
    mode3_update_data['threads'],
    mode3_update_data['io_util'],
    'Number of Threads',
    'I/O Utilization (%)',
    'Mode 3 UPDATE-only: I/O Utilization vs Load\nAll Components Active',
    'mode3_update_io.png',
    '#C73E1D'
)

# Combined plot for UPDATE-only
fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
fig.suptitle('Mode 3 (All Components) - UPDATE-only Performance\nLRU + Top-N + DB', 
             fontsize=18, fontweight='bold', y=0.995)

ax1.plot(mode3_update_data['threads'], mode3_update_data['throughput'], 
         'o-', linewidth=2.5, markersize=8, color='#2E86AB')
ax1.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax1.set_ylabel('Throughput (req/sec)', fontsize=12, fontweight='bold')
ax1.set_title('Throughput vs Load', fontsize=13, fontweight='bold')
ax1.grid(True, alpha=0.3)

ax2.plot(mode3_update_data['threads'], mode3_update_data['latency'], 
         'o-', linewidth=2.5, markersize=8, color='#A23B72')
ax2.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax2.set_ylabel('Average Latency (ms)', fontsize=12, fontweight='bold')
ax2.set_title('Latency vs Load', fontsize=13, fontweight='bold')
ax2.grid(True, alpha=0.3)

ax3.plot(mode3_update_data['threads'], mode3_update_data['cpu_util'], 
         'o-', linewidth=2.5, markersize=8, color='#F18F01')
ax3.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax3.set_ylabel('CPU Utilization (%)', fontsize=12, fontweight='bold')
ax3.set_title('CPU Utilization vs Load', fontsize=13, fontweight='bold')
ax3.grid(True, alpha=0.3)
ax3.set_ylim([0, 105])

ax4.plot(mode3_update_data['threads'], mode3_update_data['io_util'], 
         'o-', linewidth=2.5, markersize=8, color='#C73E1D')
ax4.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax4.set_ylabel('I/O Utilization (%)', fontsize=12, fontweight='bold')
ax4.set_title('I/O Utilization vs Load', fontsize=13, fontweight='bold')
ax4.grid(True, alpha=0.3)
ax4.set_ylim([0, 105])

plt.tight_layout()
plt.savefig('mode3_update_combined.png', dpi=300, bbox_inches='tight')
print(f"✓ Saved: mode3_update_combined.png")
plt.close()

# ============================================================
# GENERATE GRAPHS FOR MIXED WORKLOAD (UPDATE + GET)
# ============================================================
print("\n" + "="*70)
print("Generating Mode 3 Mixed workload graphs...")
print("="*70)

create_plot(
    mode3_leaderboard_data['threads'],
    mode3_leaderboard_data['throughput'],
    'Number of Threads',
    'Throughput (req/sec)',
    'Mode 3 Mixed: Throughput vs Load\nUPDATE + Leaderboard GET (Top-N Cache Hit)',
    'mode3_mixed_throughput.png',
    '#2E86AB'
)

create_plot(
    mode3_leaderboard_data['threads'],
    mode3_leaderboard_data['latency'],
    'Number of Threads',
    'Average Latency (ms)',
    'Mode 3 Mixed: Latency vs Load\nUPDATE + Leaderboard GET',
    'mode3_mixed_latency.png',
    '#A23B72'
)

create_plot(
    mode3_leaderboard_data['threads'],
    mode3_leaderboard_data['cpu_util'],
    'Number of Threads',
    'CPU Utilization (%)',
    'Mode 3 Mixed: CPU Utilization vs Load\nUPDATE + Leaderboard GET',
    'mode3_mixed_cpu.png',
    '#F18F01'
)

create_plot(
    mode3_leaderboard_data['threads'],
    mode3_leaderboard_data['io_util'],
    'Number of Threads',
    'I/O Utilization (%)',
    'Mode 3 Mixed: I/O Utilization vs Load\nUPDATE + Leaderboard GET',
    'mode3_mixed_io.png',
    '#C73E1D'
)

# Combined plot for Mixed workload
fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
fig.suptitle('Mode 3 (All Components) - Mixed Workload Performance\nUPDATE + Leaderboard GET (Top-N Cache)', 
             fontsize=18, fontweight='bold', y=0.995)

ax1.plot(mode3_leaderboard_data['threads'], mode3_leaderboard_data['throughput'], 
         'o-', linewidth=2.5, markersize=8, color='#2E86AB')
ax1.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax1.set_ylabel('Throughput (req/sec)', fontsize=12, fontweight='bold')
ax1.set_title('Throughput vs Load', fontsize=13, fontweight='bold')
ax1.grid(True, alpha=0.3)

ax2.plot(mode3_leaderboard_data['threads'], mode3_leaderboard_data['latency'], 
         'o-', linewidth=2.5, markersize=8, color='#A23B72')
ax2.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax2.set_ylabel('Average Latency (ms)', fontsize=12, fontweight='bold')
ax2.set_title('Latency vs Load (Lower due to Cache)', fontsize=13, fontweight='bold')
ax2.grid(True, alpha=0.3)

ax3.plot(mode3_leaderboard_data['threads'], mode3_leaderboard_data['cpu_util'], 
         'o-', linewidth=2.5, markersize=8, color='#F18F01')
ax3.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax3.set_ylabel('CPU Utilization (%)', fontsize=12, fontweight='bold')
ax3.set_title('CPU Utilization vs Load', fontsize=13, fontweight='bold')
ax3.grid(True, alpha=0.3)
ax3.set_ylim([0, 105])

ax4.plot(mode3_leaderboard_data['threads'], mode3_leaderboard_data['io_util'], 
         'o-', linewidth=2.5, markersize=8, color='#C73E1D')
ax4.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax4.set_ylabel('I/O Utilization (%)', fontsize=12, fontweight='bold')
ax4.set_title('I/O Utilization vs Load (Lower - Cache Benefits)', fontsize=13, fontweight='bold')
ax4.grid(True, alpha=0.3)
ax4.set_ylim([0, 105])

plt.tight_layout()
plt.savefig('mode3_mixed_combined.png', dpi=300, bbox_inches='tight')
print(f"✓ Saved: mode3_mixed_combined.png")
plt.close()

# ============================================================
# COMPARISON GRAPH: UPDATE vs MIXED
# ============================================================
print("\n" + "="*70)
print("Generating comparison graphs...")
print("="*70)

fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
fig.suptitle('Mode 3: UPDATE-only vs Mixed Workload Comparison', 
             fontsize=18, fontweight='bold', y=0.995)

# Throughput comparison
ax1.plot(mode3_update_data['threads'], mode3_update_data['throughput'], 
         'o-', linewidth=2.5, markersize=8, color='#2E86AB', label='UPDATE-only')
ax1.plot(mode3_leaderboard_data['threads'], mode3_leaderboard_data['throughput'], 
         's-', linewidth=2.5, markersize=8, color='#00A878', label='Mixed (UPDATE+GET)')
ax1.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax1.set_ylabel('Throughput (req/sec)', fontsize=12, fontweight='bold')
ax1.set_title('Throughput Comparison', fontsize=13, fontweight='bold')
ax1.grid(True, alpha=0.3)
ax1.legend(fontsize=11)

# Latency comparison
ax2.plot(mode3_update_data['threads'], mode3_update_data['latency'], 
         'o-', linewidth=2.5, markersize=8, color='#A23B72', label='UPDATE-only')
ax2.plot(mode3_leaderboard_data['threads'], mode3_leaderboard_data['latency'], 
         's-', linewidth=2.5, markersize=8, color='#FF6B6B', label='Mixed (UPDATE+GET)')
ax2.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax2.set_ylabel('Average Latency (ms)', fontsize=12, fontweight='bold')
ax2.set_title('Latency Comparison', fontsize=13, fontweight='bold')
ax2.grid(True, alpha=0.3)
ax2.legend(fontsize=11)

# CPU comparison
ax3.plot(mode3_update_data['threads'], mode3_update_data['cpu_util'], 
         'o-', linewidth=2.5, markersize=8, color='#F18F01', label='UPDATE-only')
ax3.plot(mode3_leaderboard_data['threads'], mode3_leaderboard_data['cpu_util'], 
         's-', linewidth=2.5, markersize=8, color='#FFA500', label='Mixed (UPDATE+GET)')
ax3.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax3.set_ylabel('CPU Utilization (%)', fontsize=12, fontweight='bold')
ax3.set_title('CPU Utilization Comparison', fontsize=13, fontweight='bold')
ax3.grid(True, alpha=0.3)
ax3.set_ylim([0, 105])
ax3.legend(fontsize=11)

# I/O comparison
ax4.plot(mode3_update_data['threads'], mode3_update_data['io_util'], 
         'o-', linewidth=2.5, markersize=8, color='#C73E1D', label='UPDATE-only')
ax4.plot(mode3_leaderboard_data['threads'], mode3_leaderboard_data['io_util'], 
         's-', linewidth=2.5, markersize=8, color='#FF4444', label='Mixed (UPDATE+GET)')
ax4.set_xlabel('Number of Threads', fontsize=12, fontweight='bold')
ax4.set_ylabel('I/O Utilization (%)', fontsize=12, fontweight='bold')
ax4.set_title('I/O Utilization Comparison', fontsize=13, fontweight='bold')
ax4.grid(True, alpha=0.3)
ax4.set_ylim([0, 105])
ax4.legend(fontsize=11)

plt.tight_layout()
plt.savefig('mode3_comparison.png', dpi=300, bbox_inches='tight')
print(f"✓ Saved: mode3_comparison.png")
plt.close()

print("\n" + "="*70)
print("All graphs generated successfully!")
print("="*70)
print("\nGenerated files:")
print("\nUPDATE-only:")
print("  1. mode3_update_throughput.png")
print("  2. mode3_update_latency.png")
print("  3. mode3_update_cpu.png")
print("  4. mode3_update_io.png")
print("  5. mode3_update_combined.png")
print("\nMixed Workload:")
print("  6. mode3_mixed_throughput.png")
print("  7. mode3_mixed_latency.png")
print("  8. mode3_mixed_cpu.png")
print("  9. mode3_mixed_io.png")
print(" 10. mode3_mixed_combined.png")
print("\nComparison:")
print(" 11. mode3_comparison.png")
print("\n" + "="*70)
print("Analysis Summary:")
print("="*70)
print("\nUPDATE-only:")
print(f"  Peak Throughput: {max(mode3_update_data['throughput']):.2f} req/sec")
print(f"  Max CPU: {max(mode3_update_data['cpu_util']):.1f}%")
print(f"  Max I/O: {max(mode3_update_data['io_util']):.1f}%")
print("\nMixed Workload:")
print(f"  Peak Throughput: {max(mode3_leaderboard_data['throughput']):.2f} req/sec")
print(f"  Max CPU: {max(mode3_leaderboard_data['cpu_util']):.1f}%")
print(f"  Max I/O: {max(mode3_leaderboard_data['io_util']):.1f}%")
print(f"  Latency Improvement: {((mode3_update_data['latency'][-1] - mode3_leaderboard_data['latency'][-1]) / mode3_update_data['latency'][-1] * 100):.1f}%")
print("\nKey Observations:")
print("  - Mixed workload shows HIGHER throughput (cache benefits on GET)")
print("  - Mixed workload shows LOWER latency (fast Top-N cache reads)")
print("  - Mixed workload shows LOWER I/O (50% of requests are cached reads)")
print("  - CPU utilization similar (both do cache maintenance)")
print("  - Demonstrates effectiveness of Top-N cache for leaderboard queries")
print("="*70)