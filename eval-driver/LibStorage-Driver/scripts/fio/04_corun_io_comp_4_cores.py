from .common import *

iotypes = ["randread"]
iosizes = ["4K"]
num_threads = ["1", "2", "4", "8", "12"]
num_tasks = 1
tsk_set = task_set()
cpus_allowed = f"{tsk_set['start']}-{tsk_set['start']+3}"

def main(baseline: bool = False):
    if baseline:
        engines = baseline_engines
    else:
        engines = aeolia_engines

    fio_main(__file__, engines, iotypes, iodepths, iosizes, num_threads, cpus_allowed, [comp])
