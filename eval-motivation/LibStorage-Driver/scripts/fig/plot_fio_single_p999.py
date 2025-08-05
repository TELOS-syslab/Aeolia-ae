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

csv_name = "01_single_thread_p999"
input_csv = os.path.join(data_dir, "fio", csv_name + ".csv")

baselines = [psync, spdk, io_uring, aeolia]
for idx, base in enumerate(baselines, start=id_start):
    base.id = idx

# Parameters used to filter data rows
iosizes = ["4K", "2M"]

iotypes_param = Param("iotype", ["randread", "randwrite"])
iodepths_param = Param("iodepth", ["1"])
iosizes_param = Param("iosize", "")
num_threads_param = Param("numjobs", ["1"])

data_files = []

for iosize in iosizes:
    output_csv = os.path.join(data_dir, f"tmp_" + csv_name + f"_{iosize}.csv")
    data_files.append(output_csv)

    iosizes_param.values = iosize
    params = [iotypes_param, iodepths_param, iosizes_param, num_threads_param]

    # Metrics used to filter data columns
    metrics = [f"{base.name}_lat_p999" for base in baselines]

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
font.xtic_font = 3
font_dict = asdict(font)

# Offset
offset_dict = asdict(offset)

plot_script = base_preset.format(root_dir=root_dir, width=width, height=height, output=output)
plot_script += base_mp_layout.format(**mp_layout_dict)
plot_script += base_style.format(border_width=border_width, line_width=line_width, boxwidth=boxwidth)
plot_script += base_font.format(**font_dict)
plot_script += base_offset.format(**offset_dict)

plot_script += """
set style data histogram
set style histogram clustered
set style fill pattern
"""

# Fist subplot
plot_script += base_next_plot.format(title="(a) 4KB")
plot_script += """
set xtics noenhanced
set ylabel "Latency (us)"
set yrange [0:]
"""
plot_script += f"plot \"{data_files[0]}\" \\"
base = baselines[0]
plot_script += base_bar.format(
    entry=f"{base.id}:xtic(1)",
    color=base.color,
    pattern=base.pattern,
    title=base.title,
)
for base in baselines[1:]:
    curve = base_bar.format(
        entry=base.id,
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve

# Second subplot
plot_script += base_next_plot.format(title="(b) [WIP] 2MB")
plot_script += """
set xtics noenhanced
set ylabel "Latency (us)"
set yrange [0:]
"""
plot_script += f"plot \"{data_files[1]}\" \\"
base = baselines[0]
plot_script += base_bar.format(
    entry=f"{base.id}:xtic(1)",
    color=base.color,
    pattern=base.pattern,
    title="",
)
for base in baselines[1:]:
    curve = base_bar.format(
        entry=base.id,
        color=base.color,
        pattern=base.pattern,
        title="",
    )
    plot_script += "'' \\" + curve

with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)

# for file in data_files:
#     os.remove(file)
