import csv
from dataclasses import dataclass, asdict
import itertools as it
from pathlib import Path

target = "pdf"
ext = ".pdf"

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
set border {border_width} linewidth {line_width}
set boxwidth {boxwidth} relative
"""

base_font = """
set key font "{key_font}"
set title  font "{title_font}"
set xlabel font "{xlabel_font}"
set ylabel font "{ylabel_font}"
set xtic   font "{xtic_font}"
set ytic   font "{ytic_font}"
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

base_line_curve = """
using {entry} with linespoints lc rgb {color} title "{title}", \
"""

base_line_labels = """
using {entry} with labels offset char {offset} tc rgb {color} notitle, \
"""

base_bar = """
using {entry} axes x1y1 lc rgb {color} fs pattern {pattern} title "{title}", \
"""

base_y2_bar = """
using {entry} axes x1y2 lc rgb {color} fs pattern {pattern} notitle, \
"""

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

# Preset
width = 5.6
height = 5.6

# Layout
mp_layout = MPLayout(
    mp_startx   =   0.1,
    mp_starty   =   0.20,
    mp_width    =   0.8,
    mp_height   =   0.50,
    mp_rowgap   =   0.05,
    mp_colgap   =   0.05,
    num_rows    =   1,
    num_cols    =   1,
)

# Style
border_width = 3
line_width = 0.5
boxwidth = 1

# Font
font = Font(
    key_font    =   ",12",
    title_font  =   ",12",
    xlabel_font =   ",12",
    ylabel_font =   ",12",
    xtic_font   =   ",12",
    ytic_font   =   ",12",
)

# Offset
offset = Offset(
    title_offset    =   "0,0",
    xlabel_offset   =   "0,0",
    ylabel_offset   =   "0,0",
    xtic_offset     =   "0,0",
    ytic_offset     =   "0,0",
)

# Baseline
id_start = 5

psync = Baseline(
    name    =   "psync",
    title   =   "psync",
    id      =   0,
    color   =   "C1",
    pattern =   1,
)
spdk = Baseline(
    name    =   "spdk",
    title   =   "spdk",
    id      =   0,
    color   =   "C2",
    pattern =   2,
)
io_uring = Baseline(
    name    =   "io_uring",
    title   =   r"io_uring",
    id      =   0,
    color   =   "C3",
    pattern =   5,
)
bypassd = Baseline(
    name    =   "bypassd",
    title   =   "bypassd",
    id      =   0,
    color   =   "C4",
    pattern =   4,
)
aeolia = Baseline(
    name    =   "aeolia",
    title   =   "aeolia",
    id      =   0,
    color   =   "C6",
    pattern =   3,
)
aeolia_sched = Baseline(
    name    =   "aeolia_sched",
    title   =   r"aeolia_sched",
    id      =   0,
    color   =   "C6",
    pattern =   3,
)

def output_name(file_path):
    file_name = Path(file_path).stem
    file_name += ext
    return file_name.replace("_", "-")

def filter_csv(input_csv, output_csv, params, metrics):
    header = ["iotype", "iodepth", "iosize", "numjobs"] + \
        metrics

    with open(input_csv, mode="r", encoding="utf-8") as infile, \
        open(output_csv, mode="w", newline="") as outfile:
        reader = csv.DictReader(infile)
        writer = csv.DictWriter(outfile, fieldnames=header)
        # writer.writeheader()
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

def trans_csv(input_csv, output_csv, params, metrics, baselines):
    header = ["metrics"]
    for base in baselines:
        header.append(base.name)

    with open(input_csv, mode="r", encoding="utf-8") as infile, \
        open(output_csv, mode="w", newline="") as outfile:
        reader = csv.DictReader(infile)
        writer = csv.DictWriter(outfile, fieldnames=header)

        rows = []
        # writer.writeheader()
        for row in reader:
            valid = True
            for para in params:
                if row[para.name] not in para.values:
                    valid = False
                    break

            if valid == True:
                for metric in metrics:
                    row_data = {
                        **{base.name: row[f"{base.name}_{metric}"] for base in baselines},
                    }
                    rows.append(row_data)
        
        for idx, row in enumerate(rows):
            filled_row = {
                "metrics": metrics[idx],
                **row,
            }
            rows[idx] = filled_row
        writer.writerow(rows[0])
        for row in rows[1:]:
            for base in baselines:
                row_key = base.name
                row[row_key] = float(row[row_key]) - float(rows[0][row_key])
            writer.writerow(row)
    