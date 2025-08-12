import atexit
import csv
import itertools as it
import os
from pathlib import Path
import psutil
from psutil._common import pcputimes
import signal
import subprocess
import sys
import time
import tempfile
from tqdm import tqdm

root_dir = os.getenv("INTUS_ROOT_DIR", "")
if root_dir == "":
    print("Please source env.sh first!")
    sys.exit(1)
results_dir = os.getenv("INTUS_RSLT_DIR")
libsched = os.getenv("INTUS_LIBSCHED")

fio_options = (
    "--lat_percentiles=1 --clat_percentiles=0 --output={log_file} --output-format=json"
)

base_config = """
[global]
ioengine={ioengine}
rw={iotype}
thread=1
group_reporting=1
percentile_list=50:99:99.5:99.9:99.99:99.999
direct=1
verify=0
runtime={runtime}s
time_based
filename={filename}
cpus_allowed_policy={policy}

[test]
iodepth={iodepth}
bs={iosize}
numjobs={numjobs}
thread
cpus_allowed={cpus_allowed}
nice={nice}
"""

base_other_test = """
[{task_name}]
new_group
iodepth={iodepth}
bs={iosize}
numjobs={numjobs}
thread
cpus_allowed={cpus_allowed}
nice={nice}
"""


def task_set():
    tsk_set = os.getenv("INTUS_TSK_SET", "")
    if tsk_set == "":
        print("Please source env.sh first!")
        sys.exit(1)
    cpu_start, cpu_end = map(int, tsk_set.split("-"))
    return {
        "start": cpu_start,
        "end": cpu_end,
    }

base_bg_command = """
nice -n 0 taskset -c {cpus_allowed} {root_dir}/swaptions \
    -ns 10000 -sm {simuls} -nt 1 -sd 0
"""

comp = "comp"

psync = "psync"
spdk = "spdk"
iou_dfl = "iou_dfl"
iou_poll = "iou_poll"
aeolia = "aeolia"

baseline_engines = [psync, iou_dfl, iou_poll, spdk]
aeolia_engines = [aeolia]
available_engines = baseline_engines + aeolia_engines

iotypes = ["randread", "randwrite"]
iodepths = ["1"]
iosizes = [
    "512B",
    "1K",
    "2K",
    "4K",
    "8K",
    "16K",
    "32K",
    "64K",
    "128K",
    "256K",
    "512K",
    "1M",
    "2M",
]


def run_fio_tests(
    eval_dir,
    pbar,
    engine,
    engine_name,
    filename,
    envs,
    engine_config,
    policy,
    iotypes,
    iodepths,
    iosizes,
    num_threads,
    cpus_allowed,
    other_tests,
):
    result_dir = os.path.join(eval_dir, engine)
    os.makedirs(result_dir, exist_ok=True)

    for iotype, iodepth, iosize, numjobs in it.product(
        iotypes, iodepths, iosizes, num_threads
    ):
        config_content = engine_config.format(
            ioengine=engine_name,
            runtime=10,
            filename=filename,
            iotype=iotype,
            iodepth=iodepth,
            iosize=iosize,
            numjobs=numjobs,
            cpus_allowed=cpus_allowed,
            policy=policy,
            nice=-1,
        )

        if other_tests != [""]:
            for test in other_tests:
                config_content += test
                if aeolia in engine:
                    config_content += "\ntype=0\nintr=1\ncoalescing=1"

        para = f"{iotype}_{iodepth}_{iosize}_{numjobs}"
        result_file = os.path.join(result_dir, f"fio_{para}.json")

        with tempfile.NamedTemporaryFile(mode="w+", suffix=".fio") as f:
            f.write(config_content)
            f.flush()

            print(config_content)
            fio_command = f"{root_dir}/../fio/fio {f.name} {fio_options.format(log_file=result_file)}"
            for k, v in envs.items():
                fio_command = f"{k}={v} " + fio_command

            pbar.set_description(
                f"Running {engine}: iotype={iotype} iodepth={iodepth} iosize={iosize} numjobs={numjobs}"
            )

            retry_num = 0
            while retry_num < 3:
                try:
                    subprocess.run(fio_command, shell=True, check=True)
                    break
                except subprocess.CalledProcessError as e:
                    print(f"Failed to run {fio_command}")
                    print(e)
                    retry_num += 1
            if retry_num == 3:
                print(f"Failed to run {fio_command}")
                exit(1)

        if aeolia in engine:
            subprocess.run(f"sudo make -C {root_dir}", shell=True, check=True)

        time.sleep(1)
        pbar.update(1)


def run_corun_with_comp_tests(
    eval_dir,
    pbar,
    engine,
    engine_name,
    filename,
    envs,
    engine_config,
    iotypes,
    iodepths,
    iosizes,
    num_threads,
    cpus_allowed,
):
    result_dir = os.path.join(eval_dir, engine)
    os.makedirs(result_dir, exist_ok=True)

    headers = [
        "cpu_utilization",
        "total_wall_time",
        "total_cpu_time",
        "user_time",
        "sys_time",
    ]

    for iotype, iodepth, iosize, numjobs in it.product(
        iotypes, iodepths, iosizes, num_threads
    ):
        config_content = engine_config.format(
            ioengine=engine_name,
            filename=filename,
            runtime=20,
            iotype=iotype,
            iodepth=iodepth,
            iosize=iosize,
            numjobs=numjobs,
            cpus_allowed=cpus_allowed,
            policy="split",
            nice=-1,
        )

        para = f"{iotype}_{iodepth}_{iosize}_{numjobs}"
        result_file = os.path.join(result_dir, f"fio_{para}.json")
        bg_result_file = os.path.join(result_dir, f"bg_{para}.csv")

        with tempfile.NamedTemporaryFile(mode="w+", suffix=".fio") as f:
            f.write(config_content)
            f.flush()

            fio_command = f"{root_dir}/../fio/fio {f.name} {fio_options.format(log_file=result_file)}"
            for k, v in envs.items():
                fio_command = f"{k}={v} " + fio_command

            simuls = 200

            bg_command = base_bg_command.format(
                cpus_allowed=cpus_allowed,
                root_dir=root_dir,
                simuls=simuls,
            )

            pbar.set_description(
                f"Running {engine}: iotype={iotype} iodepth={iodepth} iosize={iosize} numjobs={numjobs}"
            )

            # Initialize with default values
            cpu_times = psutil._common.pcputimes(
                user=0, system=0, children_user=0, children_system=0
            )

            fg = subprocess.Popen(fio_command, shell=True)

            time.sleep(5)

            start = time.time()
            bg = subprocess.Popen(
                bg_command,
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
            bg_ps = psutil.Process(bg.pid)

            def collect_stats():
                """Closure to collect stats when called"""
                nonlocal cpu_times
                try:
                    cpu_times = bg_ps.cpu_times()
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    # Fallback to basic timing if process disappeared
                    cpu_times = pcputimes(
                        user=time.time() - start,
                        system=0,
                        children_user=0,
                        children_system=0,
                    )

            # atexit.register(collect_stats)
            bg.wait()
            end = time.time()
            print(f"Swaption done {end - start}")
            # atexit.unregister(collect_stats)

            for child in psutil.Process(fg.pid).children():
                child.terminate()

            total_wall_time = end - start

            user_time = cpu_times.user
            sys_time = cpu_times.system
            total_cpu_time = user_time + sys_time
            cpu_utilization = (
                (total_cpu_time / total_wall_time) * 100 if total_wall_time > 0 else 0
            )

            with open(bg_result_file, mode="w+", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(headers)
                row_data = [
                    cpu_utilization,
                    total_wall_time,
                    total_cpu_time,
                    user_time,
                    sys_time,
                ]
                writer.writerow(row_data)

        config_content = engine_config.format(
            ioengine=engine_name,
            filename=filename,
            runtime=10,
            iotype=iotype,
            iodepth=iodepth,
            iosize=iosize,
            numjobs=numjobs,
            cpus_allowed=cpus_allowed,
            policy="split",
            nice=-1,
        )

        with tempfile.NamedTemporaryFile(mode="w+", suffix=".fio") as f:
            f.write(config_content)
            f.flush()

            fio_command = f"{root_dir}/../fio/fio {f.name} {fio_options.format(log_file=result_file)}"
            for k, v in envs.items():
                fio_command = f"{k}={v} " + fio_command

            bg_command = base_bg_command.format(
                cpus_allowed=cpus_allowed,
                root_dir=root_dir,
                simuls=10000,
            )

            print("next")
            bg = subprocess.Popen(
                bg_command,
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
            time.sleep(1)
            fg = subprocess.Popen(fio_command, shell=True)

            fg.wait()

            for child in psutil.Process(bg.pid).children():
                child.terminate()

        time.sleep(1)
        pbar.update(1)


def fio_main(
    eval_file,
    engines,
    iotypes,
    iodepths,
    iosizes,
    num_threads,
    cpus_allowed,
    other_tests,
):
    if results_dir == "":
        print("Please source env.sh first!")
        sys.exit(1)
    os.makedirs(results_dir, exist_ok=True)

    eval_name = Path(eval_file).stem
    eval_dir = os.path.join(results_dir, "fio", eval_name)
    os.makedirs(eval_dir, exist_ok=True)

    total_tests = (
        len(engines) * len(iotypes) * len(iodepths) * len(iosizes) * len(num_threads)
    )
    with tqdm(
        total=total_tests,
        desc="Running FIO Tests",
        unit="test",
        ncols=100,
        colour="green",
    ) as pbar:
        for engine in engines:
            engine_name = engine
            engine_config = base_config
            filename = os.getenv("INTUS_NVME_SSD")
            policy = "split"

            if engine == psync:
                envs = {}
            elif engine == spdk:
                pcie_addr = os.getenv("INTUS_NVME_PCIE_ADDR", "")
                traddr = os.getenv("INTUS_NVME_TR_ADDR", "")
                spdk_dir = os.getenv("INTUS_SPDK_DIR", "")

                filename = f"trtype=PCIe traddr={traddr}"
                envs = {
                    "LD_PRELOAD": f"{spdk_dir}/build/fio/spdk_nvme",
                }
                subprocess.run(
                    f"sudo PCI_ALLOWED={pcie_addr} bash {spdk_dir}/scripts/setup.sh config",
                    shell=True,
                    check=True,
                )
            elif engine == iou_dfl:
                policy = "shared"
                engine_name = "io_uring"
                envs = {}
            elif engine == iou_poll:
                policy = "shared"
                engine_name = "io_uring"
                engine_config += "\nhipri=1"
                envs = {}
            elif aeolia in engine:
                fio_engine = os.getenv("INTUS_FIO_ENGINE", "")
                engine_name = os.getenv("INTUS_FIO_ENGINE_NAME", "")
                engine_config += "\ntype=1\nintr=1\ncoalescing=1"
                # FIXME: Workaround for LibDriver
                filename = filename[:-2]
                envs = {
                    "LD_PRELOAD": fio_engine,
                }
                subprocess.run(
                    ["sudo", "make"],
                    cwd=root_dir,
                    check=True,
                )

                libsched_proc = subprocess.Popen(
                    ["sudo", libsched],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                )
                time.sleep(1)

            if comp in other_tests:
                run_corun_with_comp_tests(
                    eval_dir,
                    pbar,
                    engine,
                    engine_name,
                    filename,
                    envs,
                    engine_config,
                    iotypes,
                    iodepths,
                    iosizes,
                    num_threads,
                    cpus_allowed,
                )
            else:
                run_fio_tests(
                    eval_dir,
                    pbar,
                    engine,
                    engine_name,
                    filename,
                    envs,
                    engine_config,
                    policy,
                    iotypes,
                    iodepths,
                    iosizes,
                    num_threads,
                    cpus_allowed,
                    other_tests,
                )

            if engine == spdk:
                spdk_dir = os.getenv("INTUS_SPDK_DIR", "")
                subprocess.run(
                    f"sudo bash {spdk_dir}/scripts/setup.sh reset",
                    shell=True,
                    check=True,
                )

            if aeolia in engine and libsched_proc:
                libsched_proc.terminate()

    print("All tests completed and results saved.")
