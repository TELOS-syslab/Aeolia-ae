from .common import *

iotypes = ["randread"]
iosizes = ["4K"]
num_threads = ["1", "2", "4", "8", "12"]
num_tasks = 2
tsk_set = task_set()
cpus_allowed = f"{tsk_set["start"]}-{tsk_set["start"] + 3}"
tapp = base_other_test.format(task_name="tapp", iodepth="16", iosize="64K", numjobs="1", cpus_allowed=cpus_allowed, nice=0)

def main(baseline: bool = False):
    if baseline:
        engines = baseline_engines
    else:
        engines = aeolia_engines

    fio_main(__file__, engines, iotypes, iodepths, iosizes, num_threads, cpus_allowed, [tapp])
