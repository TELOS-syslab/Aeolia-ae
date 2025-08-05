import itertools as it
import matplotlib.pyplot as plt
import numpy as np
import os
import pandas as pd
from plot_fio_comon import *
import subprocess
import sys
import tempfile

root_dir = os.getenv("INTUS_ROOT_DIR", "")
if root_dir == "":
    print("Please source env.sh first!")
    sys.exit(1)

data_dir = os.getenv("INTUS_DATA_DIR", "")
plot_dir = os.getenv("INTUS_PLOT_DIR", "")
os.makedirs(plot_dir, exist_ok=True)

csv_name = "05_corun_multicore"
lapp_csv = os.path.join(data_dir, "fio", csv_name + "_0.csv")
tapp_csv = os.path.join(data_dir, "fio", csv_name + "_1.csv")

input_csv = os.path.join(data_dir, "tmp_" + csv_name + ".csv")

# Load CSVs
lapp_df = pd.read_csv(lapp_csv)
tapp_df = pd.read_csv(tapp_csv)

# Find *_bw columns
bw_cols = [col for col in lapp_df.columns if col.endswith("_bw")]

# Sum *_bw columns
for col in bw_cols:
    lapp_df[col] += tapp_df[col]

# Write result
lapp_df.to_csv(input_csv, index=False)

baselines = [psync, spdk, io_uring, aeolia]
for idx, base in enumerate(baselines, start=id_start):
    base.id = idx

# Parameters used to filter data rows
iotypes = ["randread", "randwrite"]

iotypes_param = Param("iotype", "")
iodepths_param = Param("iodepth", ["1"])
iosizes_param = Param("iosize", ["4K"])
num_threads_param = Param("numjobs", ["4"])

data_files = []

for iotype in iotypes:
    output_csv = os.path.join(data_dir, f"tmp_{iotype}_" + csv_name + ".csv")
    data_files.append(output_csv)

    iotypes_param.values = iotype
    params = [iotypes_param, iodepths_param, iosizes_param, num_threads_param]

    # Metrics used to filter data columns
    metrics = [f"{base.name}_bw" for base in baselines]
    metrics += [f"{base.name}_lat_p99" for base in baselines]

    filter_csv(input_csv, output_csv, params, metrics)

    headers = ["iotypes", "iodepth", "iosize", "numjobs"] + metrics
    with open(output_csv, "r") as f:
        rows = list(csv.reader(f))
        with open(output_csv, "w+", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(headers)
            writer.writerows(rows)

# Load the original data
data1 = pd.read_csv(data_files[0])  # Replace with your actual file path if needed

# Load the new data
data2 = pd.read_csv(data_files[1])

# Set up the figure with two subplots
fig, (ax1_main, ax2_main) = plt.subplots(1, 2, figsize=(18, 6))

# ===== First Subplot (Original Plot) =====
metrics = ['psync', 'spdk', 'io_uring', 'aeolia']
lat_p99 = [data1[f'{m}_lat_p99'].values[0] for m in metrics]
bw = [data1[f'{m}_bw'].values[0] for m in metrics]

# Positions and width for bars
x = np.arange(2)  # Two groups: Latency and Throughput
width = 0.2  # Width of each subbar

# First group: Latency (stacked bars)
for i, m in enumerate(metrics):
    # Plot top part (lat_p999)
    ax1_main.bar(x[0] + i*width, lat_p99[i], width, 
                label=f'{m} p99', 
                color=plt.cm.tab10(i), alpha=0.7, edgecolor='black')

# Second group: Throughput (simple bars)
ax1_second = ax1_main.twinx()
for i, m in enumerate(metrics):
    ax1_second.bar(x[1] + i*width, bw[i], width, label=m, 
                  color=plt.cm.tab10(i), edgecolor='black')

# Customize the first subplot
ax1_main.set_xticks(x + 1.5*width)
ax1_main.set_xticklabels(['Latency (μs)', 'Throughput (MB/s)'])
ax1_main.set_title('(a) Rand Read')

# Create simplified legend for latency group
latency_legend = [plt.Rectangle((0,0), 1, 1, fc=plt.cm.tab10(i)) for i in range(len(metrics))]
ax1_main.legend(latency_legend, metrics, loc='upper left')

# ===== Second Subplot (New Data) =====
lat_p99 = [data2[f'{m}_lat_p99'].values[0] for m in metrics]
bw = [data2[f'{m}_bw'].values[0] for m in metrics]

x_new = np.arange(2)  # Two groups: Throughput and Batch ops
width_new = 0.2  # Width of each subbar

# First group: Latency (stacked bars)
for i, m in enumerate(metrics):
    # Plot top part (lat_p999)
    ax2_main.bar(x[0] + i*width, lat_p99[i], width, 
                label=f'{m} p99', 
                color=plt.cm.tab10(i), alpha=0.7, edgecolor='black')

# Second group: Throughput (simple bars)
ax2_second = ax2_main.twinx()
for i, m in enumerate(metrics):
    ax2_second.bar(x[1] + i*width, bw[i], width, label=m, 
                  color=plt.cm.tab10(i), edgecolor='black')

# Customize the second subplot
ax2_main.set_xticks(x + 1.5*width)
ax2_main.set_xticklabels(['Latency (μs)', 'Throughput (MB/s)'])
ax2_main.set_title('(b) Rand Write')

# Add legend for second subplot
# handles3, labels3 = ax2_main.get_legend_handles_labels()
# ax2_main.legend(handles3, techs, loc='upper left')

# Adjust layout and save
plt.tight_layout()
plt.savefig('combined_performance_comparison.pdf', format='pdf')

print("Plot saved as combined_performance_comparison.pdf")
