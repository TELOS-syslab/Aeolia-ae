import csv
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

csv_name = "iouring_breakdown"
input_csv = os.path.join(data_dir, csv_name + ".csv")

iou_dfl = Baseline(
    name    =   "iou_dfl",
    title   =   r"iou_dfl",
    id      =   0,
    color   =   "C1",
    pattern =   3,
)
iou_opt = Baseline(
    name    =   "iou_opt",
    title   =   r"iou_opt",
    id      =   0,
    color   =   "C3",
    pattern =   3,
)
iou_poll = Baseline(
    name    =   "iou_poll",
    title   =   r"iou_poll",
    id      =   0,
    color   =   "C4",
    pattern =   3,
)
spdk.pattern = 3

baselines = [iou_dfl, iou_opt, iou_poll, spdk, aeolia]
for idx, base in enumerate(baselines, start=0):
    base.id = idx

# data_files = []
# with open(input_csv, "r") as file:
#     reader = csv.reader(file)
#     for row in reader:
#         base_name = row[0]
#         out_csv = os.path.join(data_dir, f"tmp_{base_name}_" + csv_name + ".csv")
#         data_files.append(out_csv)

#         with open(out_csv, "w+", newline="") as f:
#             writer = csv.writer(f)
#             writer.writerow(row)

env = {
    key: value.format(
        target=target,
    )
    for key, value in base_env.items()
}

# Preset
width = 2.4
height = 2.4
file_name = output_name(__file__)
output = os.path.join(plot_dir, file_name)

# MP Layout
mp_layout = MPLayout(
    mp_startx   =   0.15,
    mp_starty   =   0.12,
    mp_width    =   0.8,
    mp_height   =   0.7,
    mp_rowgap   =   0.05,
    mp_colgap   =   0.1,
    num_rows    =   1,
    num_cols    =   1,
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
set style fill solid border -1
"""

# Fist subplot
plot_script += base_next_plot.format(title="")
plot_script += """
set xtics noenhanced rotate by -45
unset ytics
unset grid
set ylabel "Latency (us)"
set yrange [3:10]
"""
plot_script += f"plot \"{input_csv}\" \\\n"
# for base in baselines:
#     plot_script += """"{file}" using 2:xtic(1) lc rgb {color} fs pattern 3, \\
# """.format(file=data_files[base.id], color=base.color)
plot_script += "using 2:xtic(1) lc rgb \"#FFFFFF\" notitle, \\"
labels = base_line_labels.format(
    entry="0:2:2",
    offset="0,1",
    color="C0",
)
plot_script += "\n'' \\" + labels
print(plot_script)

with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)

# for file in data_files:
#     os.remove(file)
