import itertools as it
import os
from plot_common import *
import subprocess
import sys
import tempfile

root_dir = os.getenv("INTUS_ROOT_DIR")
data_dir = os.getenv("INTUS_DATA_DIR")
data_dir = os.path.join(data_dir, "fio")
plot_dir = os.getenv("INTUS_PLOT_DIR")

os.makedirs(plot_dir, exist_ok=True)

csv_name = "02_multi_threads"
input_csv = os.path.join(data_dir, csv_name + ".csv")

baselines = [posix, iou_dfl, iou_poll, spdk, aeolia]
for idx, base in enumerate(baselines, start=id_start):
    base.id = idx
nr_base = len(baselines)

# Parameters used to filter data rows
iotypes = ["randread", "randwrite"]

iotypes_param = Param("iotype", "")
iodepths_param = Param("iodepth", ["1"])
iosizes_param = Param("iosize", ["4K"])
num_threads_param = Param("numjobs", ["1", "2", "4", "8", "16", "32"])

data_files = []

# Metrics used to filter data columns
metrics = [f"{base.name}_iops" for base in baselines]
metrics += [f"{base.name}_lat_p50" for base in baselines]
metrics += [f"{base.name}_lat_p99" for base in baselines]
metrics += [f"{base.name}_lat_p999" for base in baselines]

for iotype in iotypes:
    output_csv = os.path.join(data_dir, f"tmp_{iotype}_" + csv_name + ".csv")
    data_files.append(output_csv)

    iotypes_param.values = iotype
    params = [iotypes_param, iodepths_param, iosizes_param, num_threads_param]

    filter_csv(input_csv, output_csv, params, metrics)

env = {
    key: value.format(
        target=target,
    )
    for key, value in base_env.items()
}

# Preset
width = 4.8
height = 2.8
file_name = output_name(__file__)
output = os.path.join(plot_dir, file_name)

# MP Layout
mp_layout = MPLayout(
    mp_startx=0.074,
    mp_starty=0.055,
    mp_width=0.86,
    mp_height=0.85,
    mp_rowgap=0.135,
    mp_colgap=0.1,
    num_rows=2,
    num_cols=2,
)
mp_layout_dict = asdict(mp_layout)

# Style
border = 3
line_width = 0.5
boxwidth = 1

# Font
font_dict = asdict(font)

# Offset
offset.xtic_offset = "-2,0.4"
offset.xlabel_offset = "0,1.42"
offset.title_offset = "0,-0.6"
offset_dict = asdict(offset)

plot_script = base_preset.format(
    root_dir=root_dir, width=width, height=height, output=output
)
plot_script += base_mp_layout.format(**mp_layout_dict)
plot_script += base_style.format(
    border=border, line_width=line_width, boxwidth=boxwidth
)
plot_script += base_font.format(**font_dict)
plot_script += base_offset.format(**offset_dict)

# First subplot
plot_script += base_next_plot.format(title=r"(a) read: median latency")
plot_script += """
set key noenhanced
set ylabel "Latency (us)"
set yrange [0:32]
set ytic 8
set xtics rotate by -45
set xtic add ('' 0)
"""
plot_script += f'plot "{data_files[0]}" \\'
base = baselines[0]
plot_script += base_line_curve.format(
    entry=f"{base.id}:{base.id + len(baselines)}",
    color=base.color,
    pt=base.point_t,
    title=base.title,
)
for base in baselines[1:2]:
    curve = base_line_curve.format(
        entry=f"{base.id}:{base.id + len(baselines)}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' \\" + curve
for base in baselines[2:]:
    curve = base_line_curve_notitle.format(
        entry=f"{base.id}:{base.id + len(baselines)}",
        color=base.color,
        pt=base.point_t,
    )
    plot_script += "'' \\" + curve

# Second subplot
plot_script += base_font.format(**font_dict)
plot_script += base_next_plot.format(title=r"(b) read: tail latency (p99.9)")
plot_script += """
set key noenhanced
unset ylabel
set ytic 11
set yrange [0:44]
"""
plot_script += f'plot "{data_files[0]}" \\'
base = baselines[0]
plot_script += base_line_curve_notitle.format(
    entry=f"{base.id}:{next_col(base.id, nr_base, 3)}",
    color=base.color,
    pt=base.point_t,
    title=base.title,
)
for base in baselines[1:2]:
    curve = base_line_curve_notitle.format(
        entry=f"{base.id}:{next_col(base.id, nr_base, 3)}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' \\" + curve
for base in baselines[2:]:
    curve = base_line_curve.format(
        entry=f"{base.id}:{next_col(base.id, nr_base, 3)}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' \\" + curve

# Third subplot
plot_script += base_font.format(**font_dict)

plot_script += base_next_plot.format(title=r"(c) write: median latency")
plot_script += """
set xlabel "IOPS (k)"
set ylabel "Latency (us)"
set yrange [0:28]
set ytic 7
"""
plot_script += f'plot "{data_files[1]}" \\'
base = baselines[0]
plot_script += base_line_curve_notitle.format(
    entry=f"{base.id}:{base.id + len(baselines)}",
    color=base.color,
    pt=base.point_t,
)
for base in baselines[1:]:
    curve = base_line_curve_notitle.format(
        entry=f"{base.id}:{base.id + len(baselines)}",
        color=base.color,
        pt=base.point_t,
    )
    plot_script += "'' \\" + curve

# Fourth subplot
plot_script += base_next_plot.format(title=r"(d) write: tail latency (p99.9)")
plot_script += """
unset ylabel
set ytic 15
set yrange [0:60]
"""
plot_script += f'plot "{data_files[1]}" \\'
base = baselines[0]
plot_script += base_line_curve_notitle.format(
    entry=f"{base.id}:{next_col(base.id, nr_base, 3)}",
    color=base.color,
    pt=base.point_t,
)
for base in baselines[1:]:
    curve = base_line_curve_notitle.format(
        entry=f"{base.id}:{next_col(base.id, nr_base, 3)}",
        color=base.color,
        pt=base.point_t,
    )
    plot_script += "'' \\" + curve

with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)

for file in data_files:
    os.remove(file)
