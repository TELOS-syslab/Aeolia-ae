import itertools as it
import os
from plot_common import *
import subprocess
import sys
import tempfile

root_dir = os.getcwd()
plot_dir = os.path.join(root_dir, "fig", "fs")
os.makedirs(plot_dir, exist_ok=True)

###############################################################################
# Data Preparation
baselines = fs_baselines
nr_base = nr_fs_base
id_start = 2
for idx, base in enumerate(baselines, start=id_start):
    base.id = idx

data_dir = os.path.join("fig2-fio-multi-threads")
data_file_names = ["fio_4K_read", "fio_4K_write", "fio_2M_read", "fio_2M_write"]
data_files = data_file_path(data_dir, data_file_names, "csv")

###############################################################################
# Plot Preparation
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
    mp_startx=0.078,
    mp_starty=0.052,
    mp_width=0.86,
    mp_height=0.85,
    mp_rowgap=0.14,
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
offset.xlabel_offset = "0,0.7"
offset.title_offset = "0,-0.6"
offset_dict = asdict(offset)

plot_script = base_preset.format(width=width, height=height, output=output)
plot_script += base_mp_layout.format(**mp_layout_dict)
plot_script += base_style.format(border=border, line_width=line_width, boxwidth=boxwidth)
plot_script += base_font.format(**font_dict)
plot_script += base_offset.format(**offset_dict)

###############################################################################
# First subplot
plot_script += base_next_plot.format(title=r"(a) 4KB read")
plot_script += """
set key noenhanced
set ylabel "Throughput (GB/s)" offset 0.6,0
set yrange [0:120]
set ytic 30
"""
plot_script += f"plot \"{data_files[0]}\" skip 1 \\"
base = baselines[0]
plot_script += base_line_curve.format(
    entry=f"{base.id}:xtic(1)",
    color=base.color,
    pt=base.point_t,
    title=base.title,
)
for base in baselines[1:]:
    curve = base_line_curve.format(
        entry=f"{base.id}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' skip 1 \\" + curve

###############################################################################
# Second subplot
plot_script += base_next_plot.format(title=r"(b) 4KB write")
plot_script += """
set key noenhanced
unset ylabel
set yrange [0:100]
set ytic 25
"""
plot_script += f"plot \"{data_files[1]}\" skip 1 \\"
base = baselines[0]
plot_script += base_line_curve_notitle.format(
    entry=f"{base.id}:xtic(1)",
    color=base.color,
    pt=base.point_t,
    title=base.title,
)
for base in baselines[1:]:
    curve = base_line_curve_notitle.format(
        entry=f"{base.id}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' skip 1 \\" + curve

###############################################################################
# Third subplot
plot_script += base_font.format(**font_dict)

plot_script += base_next_plot.format(title=r"(c) 2MB read")
plot_script += """
set key noenhanced
set xlabel "# Thread"
set ylabel "Throughput (GB/s)" offset 0.6,0
set yrange [0:120]
set ytic 30
"""
plot_script += f"plot \"{data_files[2]}\" skip 1 \\"
base = baselines[0]
plot_script += base_line_curve_notitle.format(
    entry=f"{base.id}:xtic(1)",
    color=base.color,
    pt=base.point_t,
    title=base.title,
)
for base in baselines[1:]:
    curve = base_line_curve_notitle.format(
        entry=f"{base.id}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' skip 1 \\" + curve

###############################################################################
# Fourth subplot
plot_script += base_next_plot.format(title=r"(d) 2MB write")
plot_script += """
set key noenhanced
unset ylabel
set yrange [0:100]
set ytic 25
"""
plot_script += f"plot \"{data_files[3]}\" skip 1 \\"
base = baselines[0]
plot_script += base_line_curve_notitle.format(
    entry=f"{base.id}:xtic(1)",
    color=base.color,
    pt=base.point_t,
    title=base.title,
)
for base in baselines[1:]:
    curve = base_line_curve_notitle.format(
        entry=f"{base.id}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' skip 1 \\" + curve

################################################################################

# Execution
with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)

# for file in data_files:
#     os.remove(file)