from .common import *

iosizes = ["4K"]
iotypes = ["randread"]
num_threads = ["2"]
num_tasks = 1
tsk_set = task_set()
cpus_allowed = tsk_set["start"]


def main(baseline: bool = False):
    if baseline:
        engines = baseline_engines
    else:
        engines = aeolia_engines

    fio_main(
        __file__, engines, iotypes, iodepths, iosizes, num_threads, cpus_allowed, [""]
    )
