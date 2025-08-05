import itertools as it
import os
from pathlib import Path
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

csv_name = "01_single_thread_0"
input_csv = os.path.join(data_dir, "fio", csv_name + ".csv")

baselines = [psync, spdk, io_uring, aeolia]
for idx, base in enumerate(baselines, start=id_start):
    base.id = idx

# Parameters used to filter data rows
iotypes = ["randread", "randwrite"]
iodepths = ["1"]

iotypes_param = Param("iotype", "")
iodepths_param = Param("iodepth", "")
iosizes_param = Param("iosize", ["4K", "16K", "64K", "256K", "1M", "2M"])
num_threads_param = Param("numjobs", ["1"])

data_files = []

for iotype, iodepth in it.product(iotypes, iodepths):
    output_csv = os.path.join(data_dir, f"tmp_{iotype}_{iodepth}_" + csv_name + ".csv")
    data_files.append(output_csv)

    iotypes_param.values = iotype
    iodepths_param.values = iodepth
    params = [iotypes_param, iodepths_param, iosizes_param, num_threads_param]

    # Metrics used to filter data columns
    metrics = [f"{base.name}_bw" for base in baselines]

    filter_csv(input_csv, output_csv, params, metrics)

env = {
    key: value.format(
        target=target,
    )
    for key, value in base_env.items()
}

# Preset
width = 4.8
height = 2.4
file_name = output_name(__file__)
output = os.path.join(plot_dir, file_name)

# MP Layout
mp_layout = MPLayout(
    mp_startx   =   0.12,
    mp_starty   =   0.12,
    mp_width    =   0.85,
    mp_height   =   0.7,
    mp_rowgap   =   0.05,
    mp_colgap   =   0.1,
    num_rows    =   1,
    num_cols    =   2,
)
mp_layout_dict = asdict(mp_layout)

# Style
border_width = 3
line_width = 0.5
boxwidth = 1

# Font
font_dict = asdict(font)

# Offset
offset_dict = asdict(offset)

plot_preset = base_preset.format(root_dir=root_dir, width=width, height=height, output=output)
plot_mp_layout = base_mp_layout.format(**mp_layout_dict)
plot_style = base_style.format(border_width=border_width, line_width=line_width, boxwidth=boxwidth)
plot_font = base_font.format(**font_dict)
plot_offset = base_offset.format(**offset_dict)

plot_script = plot_preset + plot_mp_layout + plot_style + plot_font + plot_offset

# Fist subplot
plot_script += base_next_plot.format(title=r"(a) Rand Read")
plot_script += """
set key noenhanced
set xlabel "Block Size (bytes)"
set ylabel "Throughput (MB/s)"
"""
plot_script += f"plot \"{data_files[0]}\" \\"
plot_script += base_line_curve.format(
    entry=f"{baselines[0].id}:xtic(3)",
    color=baselines[0].color,
    title=baselines[0].title,
)
for base in baselines[1:]:
    curve = base_line_curve.format(
        entry=base.id,
        color=base.color,
        title=base.title,
    )
    plot_script += "'' \\" + curve

# Second subplot
plot_script += base_next_plot.format(title=r"(b) Rand Write")
plot_script += """
unset ylabel
"""
plot_script += f"plot \"{data_files[1]}\" \\"
plot_script += base_line_curve.format(
    entry=f"{baselines[0].id}:xtic(3)",
    color=baselines[0].color,
    title="",
)
for base in baselines[1:]:
    curve = base_line_curve.format(
        entry=base.id,
        color=base.color,
        title="",
    )
    plot_script += "'' \\" + curve

with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)

for file in data_files:
    os.remove(file)
