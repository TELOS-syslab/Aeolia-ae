import itertools as it
import os
from pathlib import Path
from plot_common import *
import subprocess
import sys
import tempfile
import csv

root_dir = os.getcwd()

data_dir = os.path.join("./data")
plot_dir = os.path.join(root_dir, "fig")

csv_name = "data_iou_spdk"
input_csv = os.path.join(data_dir, csv_name + ".csv")

# Read CSV data for labels
def read_csv_data(csv_file):
    data = {}
    with open(csv_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            category = row['Category']
            data[category] = {
                'iou_dfl_flat': float(row['iou_dfl_flat']),
                'iou_poll_flat': float(row['iou_poll_flat']),
                'spdk_flat': float(row['spdk_flat']),
                'iou_dfl': float(row['iou_dfl']),
                'iou_poll': float(row['iou_poll']),
                'spdk': float(row['spdk'])
            }
    return data

# Generate labels from CSV data
def generate_labels(csv_data):
    labels = []
    
    # First row: iou_dfl_flat device and processing
    device_time = csv_data['device']['iou_dfl_flat'] / 1000
    processing_time = csv_data['processing']['iou_dfl_flat'] / 1000
    labels.append(f'set label "{device_time:.3f}" at -1.7,2.000 font "Times New Roman,10"')
    labels.append(f'set label "{processing_time:.3f}" at -1.7,4.200 font "Times New Roman,10"')
    
    # Add other iou_dfl_flat labels (keeping original positions for now)
    labels.append(f'set label "1.126" at -1.7,4.900 font "Times New Roman,10"')
    labels.append(f'set label "0.648" at -1.7,5.800 font "Times New Roman,10"')
    labels.append(f'set label "1.789" at -1.7,7.000 font "Times New Roman,10"')
    labels.append(f'set label "0.188" at -1.7,8.150 font "Times New Roman,10"')
    
    # Second row: spdk_flat device and processing
    spdk_device_time = csv_data['device']['spdk_flat'] / 1000
    spdk_processing_time = csv_data['processing']['spdk_flat'] / 1000
    labels.append(f'set label "{spdk_device_time:.3f}" at -0.8,2.000 font "Times New Roman,10"')
    labels.append(f'set label "{spdk_processing_time:.3f}" at -0.76,4.000 font "Times New Roman,10"')
    
    # Total time: iou_dfl others and spdk processing
    iou_dfl_others = csv_data['others']['iou_dfl'] / 1000
    spdk_total_processing = csv_data['processing']['spdk'] / 1000
    labels.append(f'set label "{iou_dfl_others:.1f}" at -2.2,8.500 font "Times New Roman,10"')
    labels.append(f'set label "{spdk_total_processing:.1f}" at -0.2,4.90 font "Times New Roman,10"')
    
    return '\n'.join(labels) + '\n'

# Read CSV data
csv_data = read_csv_data(input_csv)

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
    mp_startx   =   0.115,
    mp_starty   =   0.05,
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
offset.ylabel_offset = "0.5,0"
offset_dict = asdict(offset)

plot_script = base_preset.format(root_dir=root_dir, width=width, height=height, output=output)
plot_script += base_mp_layout.format(**mp_layout_dict)
plot_script += base_style.format(border=border, line_width=line_width, boxwidth=boxwidth)
plot_script += base_font.format(**font_dict)
plot_script += base_offset.format(**offset_dict)

plot_script += """
set style fill pattern
"""

# set key at -1.2,8 maxrows 3 width -4 height -1.5

plot_script += base_next_plot_notitle
plot_script += """
set key right top
set boxwidth 0.4 absolute
set xtics noenhanced rotate by -45
set xtics add ('' -3, 'iou_dfl' -2, '' -1, 'spdk' 0, '' 1, '' 2, '' 3)
set xrange [-3:3]
set ylabel "Latency (us)"
set yrange [0:8.2]
set ytic 2
"""

# plot_script += f"""
# set label "3.92" at -1.7,2.000 font "Times New Roman,10"
# set label "0.529" at -1.7,4.200 font "Times New Roman,10"
# set label "1.126" at -1.7,4.900 font "Times New Roman,10"
# set label "0.648" at -1.7,5.800 font "Times New Roman,10"
# set label "1.789" at -1.7,7.000 font "Times New Roman,10"
# set label "0.188" at -1.7,8.150 font "Times New Roman,10"

# set label "3.92" at -0.8,2.000 font "Times New Roman,10"
# set label "0.28" at -0.76,4.000 font "Times New Roman,10"

# set label "8.2" at -2.2,8.500 font "Times New Roman,10"
# set label "4.2" at -0.2,4.50 font "Times New Roman,10"
# """

# set label "3.92" at -0.76,2.000 font "Times New Roman,10"
# set label "0.51" at -0.72,4.200 font "Times New Roman,10"
# set label "0.637" at -0.8,4.800 font "Times New Roman,10"
# set label "0.162" at -0.8,5.300 font "Times New Roman,10"
# set label "5.229" at -0.25,5.500 font "Times New Roman,10"
# Generate labels from CSV data
# print(generate_labels(csv_data))
plot_script += generate_labels(csv_data)
print("*********************")

###############################################################################

plot_script += f"plot \"{input_csv}\" \\"
entry = f"($0 - 2):{div_1000(5)}"
plot_script += base_y1_bar.format(
    row=6,
    entry=entry,
    color="\"#CCCCCC\"",
    pattern=2,
    title="others"
)
plot_script += "'' \\"
plot_script += base_y1_bar.format(
    row=5,
    entry=entry,
    color="\"#00CC66\"",
    pattern=1,
    title="scheduling"
)
plot_script += "'' \\"
plot_script += base_y1_bar.format(
    row=4,
    entry=entry,
    color="\"#006666\"",
    pattern=7,
    title="interrupt"
)
plot_script += "'' \\"
plot_script += base_y1_bar.format(
    row=3,
    entry=entry,
    color="\"#FF3333\"",
    pattern=4,
    title="k\_overhead"
)
# plot_script += "'' \\"
# plot_script += base_y1_bar.format(
#     row=3,
#     entry=entry,
#     color="\"#006666\"",
#     pattern=5,
#     title="iou callback"
# )
plot_script += "'' \\"
plot_script += base_y1_bar.format(
    row=2,
    entry=entry,
    color="\"#007FFF\"",
    pattern=6,
    title="processing"
)
plot_script += "'' \\"
plot_script += base_y1_bar.format(
    row=1,
    entry=entry,
    color="\"#FFB366\"",
    pattern=7,
    title="device"
)

###############################################################################

# entry = f"($0):{div_1000(6)}"
# plot_script += "'' \\"
# plot_script += base_y1_bar_notitle.format(
#     row=5,
#     entry=entry,
#     color="C2",
#     pattern=2,
#     title="kernel cross"
# )
# plot_script += "'' \\"
# plot_script += base_y1_bar_notitle.format(
#     row=3,
#     entry=entry,
#     color="C4",
#     pattern=4,
#     title="layering"
# )
# plot_script += "'' \\"
# plot_script += base_y1_bar_notitle.format(
#     row=2,
#     entry=entry,
#     color="C5",
#     pattern=5,
#     title="processing"
# )
# plot_script += "'' \\"
# plot_script += base_y1_bar_notitle.format(
#     row=1,
#     entry=entry,
#     color="C6",
#     pattern=6,
#     title="device"
# )

###############################################################################

plot_script += "'' \\"
entry = f"($0):{div_1000(7)}"
plot_script += base_y1_bar_notitle.format(
    row=2,
    entry=entry,
    color="\"#007FFF\"",
    pattern=6,
)
plot_script += "'' \\"
plot_script += base_y1_bar_notitle.format(
    row=1,
    entry=entry,
    color="\"#FFB366\"",
    pattern=7,
)

print(plot_script)

with tempfile.NamedTemporaryFile(mode="w+", suffix=".gp") as f:
    f.write(plot_script)
    f.flush()
    subprocess.run(["gnuplot", f.name], env=env)