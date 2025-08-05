import csv
from dataclasses import dataclass, asdict
import itertools as it
import os
from pathlib import Path
import shutil
import tempfile

target = "pdf"
ext = ".pdf"

###############################################################################
# Gnuplot Helpers

base_env = {
    "TARGET": "{target}",
}

base_preset = """\
call "{root_dir}/scripts/fig/common.gnuplot" "{width}in, {height}in"
set output "{output}"
set datafile separator ","
"""

base_mp_layout = """
mp_startx={mp_startx}
mp_starty={mp_starty}
mp_width={mp_width}
mp_height={mp_height}
mp_rowgap={mp_rowgap}
mp_colgap={mp_colgap}
eval mpSetup({num_cols}, {num_rows})
"""

base_style = """
set key samplen 1 left top
set key noenhanced
set border {border} linewidth {line_width}
set boxwidth {boxwidth} relative
"""

base_font = """
set key font "{key_font}"
set title  font "{title_font}"
set xlabel font "{xlabel_font}"
set ylabel font "{ylabel_font}"
set xtic   font "{xtic_font}"
set ytic   font "{ytic_font}"
set label font "{label_font}"
"""

base_offset = """
set title  offset {title_offset}
set xlabel offset {xlabel_offset}
set ylabel offset {ylabel_offset}
set xtic   offset {xtic_offset}
set ytic   offset {ytic_offset}
"""

base_next_plot = """
eval mpNext
set title "{title}"
"""

base_next_plot_notitle = """
eval mpNext
"""

base_line_curve = """
using {entry} with linespoints lc rgb {color} pt {pt} title "{title}", \
"""

base_line_curve_notitle = """
using {entry} with linespoints lc rgb {color} pt {pt} notitle, \
"""

base_line_labels = """
using {entry} with labels offset char {offset} tc rgb {color} font "Times New Roman,10" notitle, \
"""

base_bar = """
using {entry} axes x1y1 lc rgb {color} fs pattern {pattern} title "{title}", \
"""

base_bar_notitle = """
using {entry} axes x1y1 lc rgb {color} fs pattern {pattern} notitle, \
"""

base_y1_bar = """
every ::{row}::{row} using {entry} with boxes axes x1y1 lc rgb {color} fs pattern {pattern} title "{title}", \
"""

base_y1_bar_notitle = """
every ::{row}::{row} using {entry} with boxes axes x1y1 lc rgb {color} fs pattern {pattern} notitle, \
"""

base_y2_bar = """
every ::{row}::{row} using {entry} with boxes axes x1y2 lc rgb {color} fs pattern {pattern} notitle, \
"""

###############################################################################
# Classes


@dataclass
class MPLayout:
    mp_startx: float
    mp_starty: float
    mp_width: float
    mp_height: float
    mp_rowgap: float
    mp_colgap: float
    num_rows: int
    num_cols: int


@dataclass
class Font:
    key_font: str
    title_font: str
    xlabel_font: str
    ylabel_font: str
    xtic_font: str
    ytic_font: str
    label_font: str


@dataclass
class Offset:
    title_offset: str
    xlabel_offset: str
    ylabel_offset: str
    xtic_offset: str
    ytic_offset: str


@dataclass
class Param:
    name: str
    values: list[str]


@dataclass
class Baseline:
    name: str
    title: str
    id: int
    color: str
    pattern: int
    point_t: int


###############################################################################
# Preset
width = 5.6
height = 5.6

# Layout
mp_layout = MPLayout(
    mp_startx=0.1,
    mp_starty=0.20,
    mp_width=0.8,
    mp_height=0.50,
    mp_rowgap=0.05,
    mp_colgap=0.05,
    num_rows=1,
    num_cols=1,
)

# Style
border = 3
line_width = 0.5
boxwidth = 1

# Font
font = Font(
    key_font="Times New Roman,12",
    title_font="Times New Roman,12",
    xlabel_font="Times New Roman,12",
    ylabel_font="Times New Roman,12",
    xtic_font="Times New Roman,12",
    ytic_font="Times New Roman,12",
    label_font="Times New Roman,12",
)

# Offset
offset = Offset(
    title_offset="0,0",
    xlabel_offset="0,0",
    ylabel_offset="0,0",
    xtic_offset="0,0",
    ytic_offset="0,0",
)

###############################################################################
# Driver Baselines
id_start = 5

posix = Baseline(
    name="psync",
    title="posix",
    id=0,
    color="C1",
    pattern=1,
    point_t=1,
)
spdk = Baseline(
    name="spdk",
    title="spdk",
    id=0,
    color="C2",
    pattern=2,
    point_t=2,
)
iou_dfl = Baseline(
    name="iou_dfl",
    title=r"iou_dfl",
    id=0,
    color="C3",
    pattern=5,
    point_t=3,
)
iou_opt = Baseline(
    name="iou_opt",
    title=r"iou_opt",
    id=0,
    color="C5",
    pattern=5,
    point_t=3,
)
iou_poll = Baseline(
    name="iou_poll",
    title=r"iou_poll",
    id=0,
    color="C4",
    pattern=4,
    point_t=8,
)
bypassd = Baseline(
    name="bypassd",
    title="bypassd",
    id=0,
    color="C4",
    pattern=4,
    point_t=0,
)
aeolia = Baseline(
    name="aeolia",
    title="aeolia",
    id=0,
    color="C6",
    pattern=3,
    point_t=4,
)
aeolia_sched = Baseline(
    name="aeolia_sched",
    title=r"aeolia_sched",
    id=0,
    color="C6",
    pattern=3,
    point_t=4,
)

drv_baselines = [posix, iou_dfl, iou_poll, spdk, aeolia]
nr_drv_base = len(drv_baselines)

###############################################################################
# FS Baselines
ext4 = Baseline(
    name="ext4",
    title="ext4",
    id=0,
    color="C7",
    pattern=6,
    point_t=6,
)
f2fs = Baseline(
    name="f2fs",
    title="f2fs",
    id=0,
    color="C8",
    pattern=7,
    point_t=8,
)
ufs = Baseline(
    name="ufs",
    title="ufs",
    id=0,
    color="C9",
    pattern=8,
    point_t=12,
)
aeofs = Baseline(
    name="aeofs",
    title="aeofs",
    id=0,
    color="C6",
    pattern=3,
    point_t=4,
)

fs_baselines = [ext4, f2fs, ufs, aeofs]
nr_fs_base = len(fs_baselines)


###############################################################################
# Data Helpers
def next_col(x, n, m):
    return x + n * m


def next_row_g(x, n, m):
    return x + n * m


def div_1000(col):
    return f"(${col} / 1000)"


def map_to_interval(col, old_min, old_max, new_min, new_max):
    return f"(${col} >= {old_min} ? ({new_min} + ({new_max} - {new_min}) / ({old_max} - {old_min}) * (${col} - {old_min})) : ${col} / 1000)"


def bar_shift(i, n, base, step, is_left):
    if is_left == True:
        return f"($0 - ({base - 1} + {n - i} * {step}))"
    else:
        return f"($0 + ({base} + {i} * {step}))"


def output_name(file_path):
    file_name = Path(file_path).stem
    file_name += ext
    return file_name.replace("_", "-")


def data_file_path(data_dir, file_names, format):
    file_paths = []
    for file_name in file_names:
        file_paths.append(os.path.join(data_dir, f"{file_name}.{format}"))
    return file_paths


def filter_csv(input_csv, output_csv, params, metrics):
    header = ["iotype", "iodepth", "iosize", "numjobs"] + metrics

    with open(input_csv, mode="r", encoding="utf-8") as infile, open(
        output_csv, mode="w", newline=""
    ) as outfile:
        reader = csv.DictReader(infile)
        writer = csv.DictWriter(outfile, fieldnames=header)

        for row in reader:
            valid = True
            for para in params:
                if row[para.name] not in para.values:
                    valid = False
                    break

            if valid == True:
                filtered_row = {
                    **{param.name: row[param.name] for param in params},
                    **{col: row[col] for col in metrics},
                }
                writer.writerow(filtered_row)


def filter_csv_trans(input_csv, output_csv, params, metrics):
    header = ["iotype", "iodepth", "iosize", "numjobs"] + metrics

    with open(input_csv, mode="r", encoding="utf-8") as infile, open(
        output_csv, mode="w", newline=""
    ) as outfile:
        reader = csv.DictReader(infile)
        writer = csv.DictWriter(outfile, fieldnames=header)
        writer.writeheader()
        for row in reader:
            valid = True
            for para in params:
                if row[para.name] not in para.values:
                    valid = False
                    break

            if valid == True:
                filtered_row = {
                    **{param.name: row[param.name] for param in params},
                    **{col: row[col] for col in metrics},
                }
                writer.writerow(filtered_row)


def trans_csv(input_csv, output_csv, params, metrics):
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".csv", delete=False, newline=""
    ) as temp_file:
        temp_csv_path = temp_file.name
        filter_csv_trans(input_csv, temp_csv_path, params, metrics)

        with open(temp_csv_path, mode="r", encoding="utf-8") as infile, open(
            output_csv, mode="w", newline=""
        ) as outfile:
            reader = csv.DictReader(infile)

            for row in reader:
                for metric in metrics:
                    if metric in row:
                        outfile.write(f"{metric},{row[metric]}\n")
