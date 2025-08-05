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

data_dir = os.path.join("fig3-fxmark")
benchmarks = ["DWTL", "MRPL", "MRPM", "MRPH", "MRDL", "MRDM", "MWCL", "MWCM", "MWUL", "MWUM", "MWRL", "MWRM"]
# benchmarks = ["MRPL", "MRPM", "MRPH", "MWCL", "MWCM", "MWUL", "MWUM", "MWRL", "MWRM"]
data_file_names = [f"fxmark_{bench}" for bench in benchmarks]
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
height = 3.2
file_name = output_name(__file__)
output = os.path.join(plot_dir, file_name)

# MP Layout
mp_layout = MPLayout(
    mp_startx=0.08,
    mp_starty=0.04,
    mp_width=0.86,
    mp_height=0.88,
    mp_rowgap=0.12,
    mp_colgap=0.06,
    num_rows=3,
    num_cols=3,
)
mp_layout_dict = asdict(mp_layout)

# Style
border = 3
line_width = 0.5
boxwidth = 1

# Font
font_dict = asdict(font)

# Offset
offset.xlabel_offset = "0,0.9"
offset.title_offset = "0,-0.75"
offset.xtic_offset = "0,0.3"
offset_dict = asdict(offset)

plot_script = base_preset.format(width=width, height=height, output=output)
plot_script += base_mp_layout.format(**mp_layout_dict)
plot_script += base_style.format(border=border, line_width=line_width, boxwidth=boxwidth)
plot_script += base_font.format(**font_dict)
plot_script += base_offset.format(**offset_dict)

###############################################################################
# # First subplot
# plot_script += base_next_plot.format(title=r"DWTL")
# plot_script += """
# set key noenhanced
# set ylabel "ops/us"

# """
# plot_script += f"plot \"{data_files[0]}\" skip 1 \\"
# base = baselines[0]
# plot_script += base_line_curve.format(
#     entry=f"{base.id}:xtic(1)",
#     color=base.color,
#     pt=base.point_t,
#     title=base.title,
# )
# for base in baselines[1:]:
#     curve = base_line_curve.format(
#         entry=f"{base.id}",
#         color=base.color,
#         pt=base.point_t,
#         title=base.title,
#     )
#     plot_script += "'' \\" + curve

###############################################################################
# First subplot
plot_script += base_next_plot.format(title=r"MRPL")
plot_script += """
set key noenhanced
set ylabel "ops/us"
set yrange [0:402]
set ytic 100
"""
plot_script += f"plot \"{data_files[1]}\" \\"
base = baselines[0]
plot_script += base_line_curve.format(
    entry=f"{base.id}:xtic(1)",
    color=base.color,
    pt=base.point_t,
    title=base.title,
)
for base in baselines[1:2]:
    curve = base_line_curve.format(
        entry=f"{base.id}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' \\" + curve
for base in baselines[2:]:
    curve = base_line_curve_notitle.format(
        entry=f"{base.id}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' \\" + curve

###############################################################################
# Second subplot
plot_script += base_next_plot.format(title=r"MRPM")
plot_script += """
set key noenhanced
unset ylabel
set yrange [0:100]
set ytic 25
"""
plot_script += f"plot \"{data_files[2]}\" \\"
base = baselines[0]
plot_script += base_line_curve_notitle.format(
    entry=f"{base.id}:xtic(1)",
    color=base.color,
    pt=base.point_t,
    title=base.title,
)
for base in baselines[1:2]:
    curve = base_line_curve_notitle.format(
        entry=f"{base.id}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' \\" + curve
for base in baselines[2:]:
    curve = base_line_curve.format(
        entry=f"{base.id}",
        color=base.color,
        pt=base.point_t,
        title=base.title,
    )
    plot_script += "'' \\" + curve

###############################################################################
# Third subplot
plot_script += base_next_plot.format(title=r"MRPH")
plot_script += """
set key noenhanced
set yrange [0:120]
set ytic 30
"""
plot_script += f"plot \"{data_files[3]}\" \\"
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
    plot_script += "'' \\" + curve

###############################################################################
###############################################################################
# Fifth subplot
# plot_script += base_font.format(**font_dict)
# plot_script += base_next_plot.format(title=r"MRDL")
# plot_script += """
# set key noenhanced
# set ylabel "ops/us"

# """
# plot_script += f"plot \"{data_files[4]}\" skip 1 \\"
# base = baselines[0]
# plot_script += base_line_curve_notitle.format(
#     entry=f"{base.id}:xtic(1)",
#     color=base.color,
#     pt=base.point_t,
#     title=base.title,
# )
# for base in baselines[1:]:
#     curve = base_line_curve_notitle.format(
#         entry=f"{base.id}",
#         color=base.color,
#         pt=base.point_t,
#         title=base.title,
#     )
#     plot_script += "'' \\" + curve

###############################################################################
# Sixth subplot
# plot_script += base_next_plot.format(title=r"MRDM")
# plot_script += """
# set key noenhanced
# unset ylabel

# """
# plot_script += f"plot \"{data_files[5]}\" \\"
# base = baselines[0]
# plot_script += base_line_curve_notitle.format(
#     entry=f"{base.id}:xtic(1)",
#     color=base.color,
#     pt=base.point_t,
#     title=base.title,
# )
# for base in baselines[1:]:
#     curve = base_line_curve_notitle.format(
#         entry=f"{base.id}",
#         color=base.color,
#         pt=base.point_t,
#         title=base.title,
#     )
#     plot_script += "'' \\" + curve

###############################################################################
# Fourth subplot
plot_script += base_font.format(**font_dict)
plot_script += base_next_plot.format(title=r"MWCL")
plot_script += """
set key noenhanced
set ylabel "ops/us"
set yrange [0:3.6]
set ytic 0.9
"""
plot_script += f"plot \"{data_files[6]}\" \\"
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
    plot_script += "'' \\" + curve

###############################################################################
# Fifth subplot
plot_script += base_next_plot.format(title=r"MWCM")
plot_script += """
set key noenhanced
unset ylabel
set yrange [0:3.2]
set ytic 0.8
"""
plot_script += f"plot \"{data_files[7]}\" \\"
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
    plot_script += "'' \\" + curve

###############################################################################
###############################################################################
# Sixth subplot
plot_script += base_font.format(**font_dict)
plot_script += base_next_plot.format(title=r"MWUL")
plot_script += """
set key noenhanced
set yrange [0:20]
set ytic 5
"""
plot_script += f"plot \"{data_files[8]}\" skip 1 \\"
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
# Seventh subplot
plot_script += base_font.format(**font_dict)
plot_script += base_font.format(**font_dict)
plot_script += base_next_plot.format(title=r"MWUM")
plot_script += """
set key noenhanced
set xlabel "# Thread"
set ylabel "ops/us" offset -1,0
set yrange [0:20]
set ytic 5
"""
plot_script += f"plot \"{data_files[9]}\" \\"
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
    plot_script += "'' \\" + curve

###############################################################################
# Eighth subplot
plot_script += base_next_plot.format(title=r"MWRL")
plot_script += """
set key noenhanced
unset ylabel
set yrange [0:16.21]
set ytic 4
"""
plot_script += f"plot \"{data_files[10]}\" \\"
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
    plot_script += "'' \\" + curve

###############################################################################
# Ninth subplot
plot_script += base_next_plot.format(title=r"MWRM")
plot_script += """
set key noenhanced
set yrange [0:3.2]
set ytic 0.8
"""
plot_script += f"plot \"{data_files[11]}\" \\"
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
    plot_script += "'' \\" + curve

################################################################################
# Execution
with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)

# for file in data_files:
#     os.remove(file)