import csv
from collections import defaultdict
import itertools as it
import importlib as il
import json
import os
import sys

# Add parent directory to Python path for direct execution
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
if parent_dir not in sys.path:
    sys.path.append(parent_dir)

from common import aeolia_engines, available_engines

metrics = ["bw", "iops", "lat_avg", "lat_p50", "lat_p99", "lat_p999"]


def nested_defaultdict():
    return defaultdict(lambda: nested_defaultdict())


def parse_filename(filename):
    """
    filename format: fio_{iotype}_{iodepth}_{iosize}_{numjobs}.json
    """
    parts = filename.split("_")
    if len(parts) != 5:
        raise ValueError(f"Invalid filename: {filename}")

    iotype = parts[1]
    iodepth = parts[2]
    iosize = parts[3]
    numjobs = parts[4].split(".")[0]

    return {
        "iotype": iotype,
        "iodepth": iodepth,
        "iosize": iosize,
        "numjobs": numjobs,
    }


def parse_data(json_data, i):
    job = json_data["jobs"][i]
    user_cpu = job["usr_cpu"]
    sys_cpu = job["sys_cpu"]
    io = job["write"] if "write" in json_data["global options"]["rw"] else job["read"]

    return {
        "bw": str(io["bw"] / 1000),
        "iops": str(io["iops"] / 1000),
        "lat_avg": str(io["lat_ns"]["mean"] / 1000),
        "lat_p50": str(io["lat_ns"]["percentile"]["50.000000"] / 1000),
        "lat_p99": str(io["lat_ns"]["percentile"]["99.000000"] / 1000),
        "lat_p999": str(io["lat_ns"]["percentile"]["99.900000"] / 1000),
        "lat_p9999": str(io["lat_ns"]["percentile"]["99.990000"] / 1000),
        "usr_cpu": str(user_cpu),
        "sys_cpu": str(sys_cpu),
    }


def load_json_file(file_path):
    with open(file_path, "r", encoding="utf-8") as file:
        lines = file.readlines()

        for i in range(len(lines)):
            try:
                json_content = "".join(lines[i:])
                data = json.loads(json_content)
                return data
            except json.JSONDecodeError:
                continue

    raise ValueError("No valid JSON found in the file.")


def process_directory(directory, num_tasks):
    data = nested_defaultdict()

    with os.scandir(directory) as entries:
        for subdir in entries:
            if subdir.is_dir():
                engine = subdir.name
                with os.scandir(subdir.path) as files:
                    for file in files:
                        if file.is_file():
                            file_name = file.name
                            if file_name.startswith("fio_") and file_name.endswith(
                                ".json"
                            ):
                                file_path = file.path
                                file_info = parse_filename(file_name)
                                json_data = load_json_file(file_path)

                                for i in range(num_tasks):
                                    metrics_data = parse_data(json_data, i)
                                    data[engine][file_info["iotype"]][
                                        file_info["iodepth"]
                                    ][file_info["iosize"]][file_info["numjobs"]][
                                        i
                                    ] = metrics_data

    return data


def process_directory_comp(directory, num_tasks):
    data = nested_defaultdict()
    bg_data = nested_defaultdict()

    with os.scandir(directory) as entries:
        for subdir in entries:
            if subdir.is_dir():
                engine = subdir.name
                with os.scandir(subdir.path) as files:
                    for file in files:
                        if file.is_file():
                            file_name = file.name
                            if file_name.startswith("fio_") and file_name.endswith(
                                ".json"
                            ):
                                file_path = file.path
                                file_info = parse_filename(file_name)
                                json_data = load_json_file(file_path)

                                for i in range(num_tasks):
                                    metrics_data = parse_data(json_data, i)
                                    data[engine][file_info["iotype"]][
                                        file_info["iodepth"]
                                    ][file_info["iosize"]][file_info["numjobs"]][
                                        i
                                    ] = metrics_data
                            elif file_name.startswith("bg_") and file_name.endswith(
                                ".csv"
                            ):
                                file_path = file.path
                                file_info = parse_filename(file_name)
                                with open(
                                    file_path, mode="r", newline="", encoding="utf-8"
                                ) as f:
                                    reader = csv.DictReader(f)
                                    first_row = next(reader, None)
                                    batch_ops = 10000 / float(
                                        first_row["total_wall_time"]
                                    )
                                    bg_data[engine][file_info["iotype"]][
                                        file_info["iodepth"]
                                    ][file_info["iosize"]][
                                        file_info["numjobs"]
                                    ] = batch_ops

    return (data, bg_data)


def extract_data_to_tables(
    data, engines, iotypes, iodepths, iosizes, num_threads, num_tasks
):
    tmp_tables = nested_defaultdict()
    tables = nested_defaultdict()

    for engine, iotype, iodepth, iosize, numjobs, i in it.product(
        engines, iotypes, iodepths, iosizes, num_threads, range(num_tasks)
    ):
        row_key = (iotype, iodepth, iosize, numjobs)
        io_data = data[engine][iotype][iodepth][iosize][numjobs][i]

        for metric in metrics:
            tmp_tables[i][row_key][metric][engine] = io_data[metric]

    for engine, iotype, iodepth, iosize, numjobs in it.product(
        engines, iotypes, iodepths, iosizes, num_threads
    ):
        row_key = (iotype, iodepth, iosize, numjobs)

        for metric in metrics:
            tables[row_key][metric][engine] = tmp_tables[0][row_key][metric][engine]
            if num_tasks > 1 and metric == "bw":
                lapp_bw = float(tables[row_key][metric][engine])
                tapp_bw = float(tmp_tables[1][row_key][metric][engine])
                tables[row_key][metric][engine] = str(lapp_bw + tapp_bw)

    return tables


def extract_data_to_tables_comp(
    data, bg_data, engines, iotypes, iodepths, iosizes, num_threads, num_tasks
):
    tmp_tables = nested_defaultdict()
    tables = nested_defaultdict()

    for engine, iotype, iodepth, iosize, numjobs, i in it.product(
        engines, iotypes, iodepths, iosizes, num_threads, range(num_tasks)
    ):
        row_key = (iotype, iodepth, iosize, numjobs)
        io_data = data[engine][iotype][iodepth][iosize][numjobs][i]
        bg_ops = bg_data[engine][iotype][iodepth][iosize][numjobs]
        for metric in metrics:
            tmp_tables[i][row_key][metric][engine] = io_data[metric]
        tmp_tables[i][row_key]["bg"][engine] = bg_ops

    for engine, iotype, iodepth, iosize, numjobs in it.product(
        engines, iotypes, iodepths, iosizes, num_threads
    ):
        row_key = (iotype, iodepth, iosize, numjobs)

        for metric in metrics:
            tables[row_key][metric][engine] = tmp_tables[0][row_key][metric][engine]
        tables[row_key]["bg"][engine] = tmp_tables[0][row_key]["bg"][engine]

    return tables


def write_tables_to_csv(eval_name, tables, output_dir, engines):
    os.makedirs(output_dir, exist_ok=True)

    headers = ["iotype", "iodepth", "iosize", "numjobs"] + [
        f"{engine}_{metric}" for metric, engine in it.product(metrics, engines)
    ]

    csv_file = os.path.join(output_dir, f"{eval_name}.csv")

    with open(csv_file, mode="w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(headers)
        for row_key, row_data in tables.items():
            row = list(row_key)
            row.extend(
                row_data[metric].get(engine, "")
                for metric, engine in it.product(metrics, engines)
            )
            writer.writerow(row)


def write_tables_to_csv_comp(eval_name, tables, output_dir, engines):
    os.makedirs(output_dir, exist_ok=True)

    headers = (
        ["iotype", "iodepth", "iosize", "numjobs"]
        + [f"{engine}_{metric}" for metric, engine in it.product(metrics, engines)]
        + [f"{engine}_bg" for engine in engines]
    )

    csv_file = os.path.join(output_dir, f"{eval_name}.csv")

    with open(csv_file, mode="w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(headers)
        for row_key, row_data in tables.items():
            row = list(row_key)
            row.extend(
                row_data[metric].get(engine, "")
                for metric, engine in it.product(metrics, engines)
            )
            row.extend(row_data["bg"].get(engine, "") for engine in engines)
            writer.writerow(row)


results_dir = os.getenv("INTUS_RSLT_DIR", "")
if results_dir == "":
    print("Please source env.sh first!")
    sys.exit(1)
results_dir = os.path.join(results_dir, "fio")

data_dir = os.getenv("INTUS_DATA_DIR")
data_dir = os.path.join(data_dir, "fio")
os.makedirs(data_dir, exist_ok=True)

with os.scandir(results_dir) as entries:
    for entry in entries:
        if entry.is_dir():
            eval_name = entry.name

            print(f"Parse {eval_name} evaluation results")

            module = il.import_module(f"fio.{eval_name}")
            engines = available_engines
            iotypes = module.iotypes
            iodepths = module.iodepths
            iosizes = module.iosizes
            num_threads = module.num_threads
            num_tasks = module.num_tasks

            if "03" in eval_name:
                iosizes += ["128K"]

            if (
                "01" in eval_name
                or "02" in eval_name
                or "05" in eval_name
                or "06" in eval_name
            ):
                data = process_directory(entry.path, num_tasks)
                tables = extract_data_to_tables(
                    data, engines, iotypes, iodepths, iosizes, num_threads, num_tasks
                )
                write_tables_to_csv(eval_name, tables, data_dir, engines)
            elif "03" in eval_name or "04" in eval_name:
                (data, bg_data) = process_directory_comp(entry.path, num_tasks)
                tables = extract_data_to_tables_comp(
                    data,
                    bg_data,
                    engines,
                    iotypes,
                    iodepths,
                    iosizes,
                    num_threads,
                    num_tasks,
                )
                write_tables_to_csv_comp(eval_name, tables, data_dir, engines)
