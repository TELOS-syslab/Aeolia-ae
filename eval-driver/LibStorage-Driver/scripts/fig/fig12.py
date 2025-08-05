import itertools as it
import os
from pathlib import Path
from plot_common import *
import subprocess
import sys
import tempfile

root_dir = os.getenv("INTUS_ROOT_DIR")
data_dir = os.getenv("INTUS_DATA_DIR")
data_dir = os.path.join(data_dir, "fio")
plot_dir = os.getenv("INTUS_PLOT_DIR")

os.makedirs(plot_dir, exist_ok=True)

baselines = [posix, iou_dfl, iou_poll, spdk, aeolia]
for idx, base in enumerate(baselines, start=id_start):
    base.id = idx
nr_base = len(baselines)

# Parameters used to filter data rows
iotypes_param = Param("iotype", ["randread"])
iodepths_param = Param("iodepth", ["1"])
iosizes_param = Param("iosize", ["4K"])
num_threads_param = Param("numjobs", ["1", "2", "4", "8", "12"])

data_files = []

# For single core co-run
csv_name = "03_corun_io_comp_single_core"
input_csv = os.path.join(data_dir, csv_name + ".csv")
output_csv = os.path.join(data_dir, f"tmp_" + csv_name + "single.csv")
data_files.append(output_csv)
params = [iotypes_param, iodepths_param, iosizes_param, num_threads_param]
metrics = [f"{base.name}_lat_p999" for base in baselines]
metrics += [f"{base.name}_bg" for base in baselines]
filter_csv(input_csv, output_csv, params, metrics)

# For 4-core co-run
csv_name = "04_corun_io_comp_4_cores"
input_csv = os.path.join(data_dir, csv_name + ".csv")
output_csv = os.path.join(data_dir, f"tmp_" + csv_name + "4core.csv")
data_files.append(output_csv)
params = [iotypes_param, iodepths_param, iosizes_param, num_threads_param]
metrics = [f"{base.name}_lat_p999" for base in baselines]
metrics += [f"{base.name}_bg" for base in baselines]
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
    mp_startx=0.075,
    mp_starty=0.07,
    mp_width=0.9,
    mp_height=0.83,
    mp_rowgap=0.16,
    mp_colgap=0.08,
    num_rows=2,
    num_cols=2,
)
mp_layout_dict = asdict(mp_layout)

# Style
border = 3
line_width = 0.5
boxwidth = 0.75

# Font
font_dict = asdict(font)

# Offset
offset.xlabel_offset = "0,0.6"
offset.title_offset = "0,-0.2"
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

plot_script += """
set style data histogram
set style histogram clustered
set style fill pattern
"""

# First subplot
plot_script += base_next_plot.format(title=r"(a) single core: latency-task tail latency (p99.9)")
plot_script += """
set xtics noenhanced
set ylabel "Latency (ms)" offset 2,0
set yrange [0:50]
set ytic 12.5
"""
plot_script += f'plot "{data_files[0]}" \\'
base = baselines[0]
plot_script += base_bar.format(
    entry=f"{div_1000(base.id)}:xtic(4)",
    color=base.color,
    pattern=base.pattern,
    title=base.title,
)
for base in baselines[1:]:
    curve = base_bar.format(
        entry=div_1000(base.id),
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve

# Second subplot
plot_script += base_next_plot.format(title=r"(b) single core: compute-task performance")
plot_script += """
set xtics noenhanced
set ylabel "Batch kops per second" offset 2,0
set yrange [0:2.4]
set ytic 0.6
unset arrow
"""
plot_script += f'plot "{data_files[0]}" \\'
base = baselines[0]
plot_script += base_bar_notitle.format(
    entry=f"{div_1000(next_col(base.id, nr_base, 1))}:xtic(4)",
    color=base.color,
    pattern=base.pattern,
    title=base.title,
)
for base in baselines[1:]:
    curve = base_bar_notitle.format(
        entry=div_1000(next_col(base.id, nr_base, 1)),
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve

# Third subplot
plot_script += base_next_plot.format(title=r"(c) 4 cores: latency-task tail latency (p99.9)")
plot_script += """
set xtics noenhanced
set xlabel "# Latency-Task
set ylabel "Latency (ms)" offset 2,0
set yrange [0:0.1222]
set ytic 0.03
set ytic add ('4' 0.09, '8' 0.12)

set arrow from 2.5,0.075 to 4.5,0.075 nohead lc rgb "white" lw 2.2 front
set arrow from -1.1,0.075 to -0.9,0.075 nohead lc rgb "white" lw 2.2 front

set arrow from -1.06,0.075 to -0.94,0.0775 nohead lc rgb "black" lw 0.5 front
set arrow from -1.06,0.073 to -0.94,0.0755 nohead lc rgb "black" lw 0.5 front
"""
plot_script += f'plot "{data_files[1]}" \\'
base = baselines[0]
plot_script += base_bar_notitle.format(
    entry=f"{map_to_interval(base.id, 4000, 8000, 0.09, 0.12)}:xtic(4)",
    color=base.color,
    pattern=base.pattern,
    title=base.title,
)
for base in baselines[1:2]:
    curve = base_bar_notitle.format(
        entry=map_to_interval(base.id, 4000, 8000, 0.09, 0.12),
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve
for base in baselines[2:4]:
    curve = base_bar_notitle.format(
        entry=map_to_interval(base.id, 4000, 8000, 0.09, 0.12),
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve
for base in baselines[4:]:
    curve = base_bar_notitle.format(
        entry=map_to_interval(base.id, 4000, 8000, 0.09, 0.12),
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve

# Fourth subplot
plot_script += base_next_plot.format(title=r"(d) 4 cores: compute-task performance")
plot_script += """
set xtics noenhanced
set ylabel "Batch kops per second" offset 0,0
set yrange [0:4]
set ytic 1
unset arrow
"""
plot_script += f'plot "{data_files[1]}" \\'
base = baselines[0]
plot_script += base_bar_notitle.format(
    entry=f"{div_1000(next_col(base.id, nr_base, 1))}:xtic(4)",
    color=base.color,
    pattern=base.pattern,
    title=base.title,
)
for base in baselines[1:]:
    curve = base_bar_notitle.format(
        entry=div_1000(next_col(base.id, nr_base, 1)),
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve

with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)

for file in data_files:
    os.remove(file)
