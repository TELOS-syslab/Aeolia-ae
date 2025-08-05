import itertools as it
import os
from pathlib import Path
from plot_common import *
import subprocess
import sys
import tempfile

root_dir = os.getcwd()

data_dir = os.path.join(root_dir, "data")
plot_dir = os.path.join(root_dir, "fig")

csv_name = "latency_formatted"
input_csv = os.path.join(data_dir, csv_name + ".csv")

baselines = [posix, iou_dfl, iou_opt, iou_poll, spdk, aeolia]
for idx, base in enumerate(baselines, start=0):
    base.id = idx

env = {
    key: value.format(
        target=target,
    )
    for key, value in base_env.items()
}

# Preset
width = 2.4
height = 1.6
file_name = output_name(__file__)
output = os.path.join(plot_dir, file_name)

# MP Layout
mp_layout = MPLayout(
    mp_startx   =   0.14,
    mp_starty   =   0.04,
    mp_width    =   0.8,
    mp_height   =   0.75,
    mp_rowgap   =   0.05,
    mp_colgap   =   0.1,
    num_rows    =   1,
    num_cols    =   1,
)
mp_layout_dict = asdict(mp_layout)

# Style
border = 3
line_width = 0.5
boxwidth = 0.8

# Font
font_dict = asdict(font)

# Offset
offset.xtic_offset = "-2,0.2"
offset.ylabel_offset = "1.5,0"
offset_dict = asdict(offset)

plot_script = base_preset.format(root_dir=root_dir, width=width, height=height, output=output)
plot_script += base_mp_layout.format(**mp_layout_dict)
plot_script += base_style.format(border=border, line_width=line_width, boxwidth=boxwidth)
plot_script += base_font.format(**font_dict)
plot_script += base_offset.format(**offset_dict)

plot_script += """
set style fill pattern
"""

off = 0.5

# First subplot
plot_script += base_next_plot_notitle
plot_script += """
set boxwidth 0.2 absolute
set xtics noenhanced rotate by -45
set xtics add ('' -0.5, 'posix' 0, 'iou_dfl' 0.5, 'iou_opt' 1, 'iou_poll' 1.5, 'spdk' 2, 'aeolia' 2.5, '' 3)
set xrange [-0.5:3]
set ylabel "Latency (us)"
set yrange [0:10]
set ytic 2.5
"""

plot_script += """
set label "device" at 2.64,4.5 font "Times New Roman,12" front
set arrow from -0.5,3.92 to 3,3.92 nohead ls 0 lc rgb "black" lw 0.5 front
"""

plot_script += f"plot \"{input_csv}\" \\"
base = baselines[0]
plot_script += base_y1_bar_notitle.format(
    row=base.id,
    entry=f"($0 + {base.id} * {off}):2",
    color=base.color,
    pattern=base.pattern,
)
for base in baselines[1:]:
    curve = base_y1_bar_notitle.format(
        row=base.id,
        entry=f"($0 + {base.id} * {off}):2",
        color=base.color,
        pattern=base.pattern,
    )
    plot_script += "'' \\" + curve
plot_script += "'' "
labels = base_line_labels.format(
    entry="3:2:2",
    offset="0,0.5",
    color="C0",
)
plot_script += "\\" + labels

plot_script += """
unset ytics
set ytic nomirror
set ytic add ('' 0, '' 2.5, '' 5, '' 7.5, '' 10, 3.92)
set ytic font "Times New Roman,10"
replot
"""

with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)