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

csv_name = "data_corun_io_comp"
input_csv = os.path.join(data_dir, csv_name + ".csv")

baselines = [posix, iou_dfl, iou_poll, spdk, aeolia]
id_start = 0
for idx, base in enumerate(baselines, start=id_start):
    base.id = idx

# Parameters used to filter data rows
iotypes_param = Param("iotype", ["randread"])
iodepths_param = Param("iodepth", ["1"])
num_threads_param = Param("numjobs", ["1"])

data_files = []

# For io-comp
iosizes_param = Param("iosize", ["128K"])
csv_name = "03_corun_io_comp_single_core"
input_csv = os.path.join(data_dir, csv_name + ".csv")
output_csv = os.path.join(data_dir, f"tmp_" + csv_name + "io_comp.csv")
data_files.append(output_csv)
params = [iotypes_param, iodepths_param, iosizes_param, num_threads_param]
metrics = [f"{base.name}_bw" for base in baselines]
metrics += [f"{base.name}_bg" for base in baselines]
trans_csv(input_csv, output_csv, params, metrics)

# For io-io
iosizes_param = Param("iosize", ["4K"])
num_threads_param = Param("numjobs", ["2"])
csv_name = "02_multi_threads_single_core"
input_csv = os.path.join(data_dir, csv_name + ".csv")
output_csv = os.path.join(data_dir, f"tmp_" + csv_name + "io_io.csv")
data_files.append(output_csv)
params = [iotypes_param, iodepths_param, iosizes_param, num_threads_param]
metrics = [f"{base.name}_lat_p999" for base in baselines]
metrics += [f"{base.name}_bw" for base in baselines]
trans_csv(input_csv, output_csv, params, metrics)

env = {
    key: value.format(
        target=target,
    )
    for key, value in base_env.items()
}

# Preset
width = 4.8
height = 1.6
file_name = output_name(__file__)
output = os.path.join(plot_dir, file_name)

# MP Layout
mp_layout = MPLayout(
    mp_startx=0.052,
    mp_starty=0.16,
    mp_width=0.872,
    mp_height=0.73,
    mp_rowgap=0.1,
    mp_colgap=0.2,
    num_rows=1,
    num_cols=2,
)
mp_layout_dict = asdict(mp_layout)

# Style
border = 11
line_width = 0.5
boxwidth = 0.75

# Font
font_dict = asdict(font)

# Offset
offset.xtic_offset = "0,0.1"
offset.ylabel_offset = "1.2,0"
offset.title_offset = "0,0.4"
offset_dict = asdict(offset)

plot_script = base_preset.format(root_dir=root_dir, width=width, height=height, output=output)
plot_script += base_mp_layout.format(**mp_layout_dict)
plot_script += base_style.format(border=border, line_width=line_width, boxwidth=boxwidth)
plot_script += base_font.format(**font_dict)
plot_script += base_offset.format(**offset_dict)

plot_script += """
set style fill pattern
"""

off = 0.7
left_base = 1.24
right_base = 0.94

# First subplot
plot_script += base_next_plot_notitle
plot_script += """
set boxwidth 0.5 absolute
set key at -7,4.8 maxrows 1
set xtics noenhanced
set xtics add ('' -5, '' -4, '' -3, '' -2, '' -1, '' 0, '' 1, '' 2, '' 3, '' 4, '' 5)
set y2tics
set y2tics font "Times New Roman,12" offset -0.8,0
set xrange [-5:5]
set ylabel "Throughput (GB/s)"
set y2label "Batch kops per second"
set y2label font "Times New Roman,12" offset -2,0
set yrange [0:4]
set y2range [0:3]
set ytic 1
set y2tic 0.75
"""
plot_script += f"plot \"{data_files[0]}\" \\"
base = baselines[0]
plot_script += base_y1_bar.format(
    row=base.id,
    entry=f"{bar_shift(base.id - id_start, nr_drv_base, left_base, off, True)}:($2 / 1000)",
    color=base.color,
    pattern=base.pattern,
    title=base.title,
)
for base in baselines[1:3]:
    curve = base_y1_bar.format(
        row=base.id,
        entry=f"{bar_shift(base.id - id_start, nr_drv_base, left_base, off, True)}:($2 / 1000)",
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve
for base in baselines[3:]:
    curve = base_y1_bar_notitle.format(
        row=base.id,
        entry=f"{bar_shift(base.id - id_start, nr_drv_base, left_base, off, True)}:($2 / 1000)",
        color=base.color,
        pattern=base.pattern,
    )
    plot_script += "'' \\" + curve
for base in baselines:
    curve = base_y2_bar.format(
        row=base.id + len(baselines),
        entry=f"{bar_shift(base.id - id_start, nr_drv_base, right_base, off, False)}:($2 / 1000)",
        color=base.color,
        pattern=base.pattern,
    )
    plot_script += "'' \\" + curve

plot_script += """
unset grid
set xtics add ('I/O-Task' -2.3, 'Compute-Task' 2.3)
replot
set grid
set xtic auto
"""

# Second plot
plot_script += base_next_plot_notitle
plot_script += """
set xtics noenhanced
set ylabel "Throughput (GB/s)"
set y2label "P99.9 Latency (us)"
set key at -3.8,1.2 maxrows 1
set xtics add ('' -5, '' -4, '' -3, '' -2, '' -1, '' 0, '' 1, '' 2, '' 3, '' 4, '' 5)
set ytic 0.25
set yrange [0:1]
set y2tic 10
set y2range [0:40.2]
set y2tics add ('3000' 30, '4000' 40)

set arrow from 1.5,0.675 to 3.5,0.675 nohead lc rgb "white" lw 2 front
set arrow from 4.75,0.675 to 5,0.675 nohead lc rgb "white" lw 2 front
set arrow from 4.86,0.67 to 5.14,0.694 nohead lc rgb "black" lw 0.5 front
set arrow from 4.86,0.651 to 5.14,0.675 nohead lc rgb "black" lw 0.5 front
"""
plot_script += f'plot "{data_files[1]}" \\'
base = baselines[0]
plot_script += base_y1_bar_notitle.format(
    row=base.id + len(baselines),
    entry=f"{bar_shift(base.id - id_start, nr_drv_base, left_base, off, True)}:($2 / 1000)",
    color=base.color,
    pattern=base.pattern,
)
for base in baselines[1:3]:
    curve = base_y1_bar_notitle.format(
        row=base.id + len(baselines),
        entry=f"{bar_shift(base.id - id_start, nr_drv_base, left_base, off, True)}:($2 / 1000)",
        color=base.color,
        pattern=base.pattern,
    )
    plot_script += "'' \\" + curve
for base in baselines[3:]:
    curve = base_y1_bar.format(
        row=base.id + len(baselines),
        entry=f"{bar_shift(base.id - id_start, nr_drv_base, left_base, off, True)}:($2 / 1000)",
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve
for base in baselines:
    curve = base_y2_bar.format(
        row=base.id,
        entry=f"{bar_shift(base.id - id_start, nr_drv_base, right_base, off, False)}:({map_to_interval(2, 3000, 4000, 0.03, 0.04)} * 1000)",
        color=base.color,
        pattern=base.pattern,
        title=base.title,
    )
    plot_script += "'' \\" + curve

plot_script += """
unset grid
set xtics add ('Throughput' -2.3, 'Latency' 2.3)
replot
"""

with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)

# for data_file in data_files:
#     os.remove(data_file)
