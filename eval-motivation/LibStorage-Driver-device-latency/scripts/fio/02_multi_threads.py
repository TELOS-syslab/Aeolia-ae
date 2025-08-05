from common import psync, spdk, io_uring, bypassd, aeolia, aeolia_sched, \
    task_set, fio_main

# engines = [psync, spdk, io_uring, bypassd, aeolia]
engines = [aeolia]
iotypes = ["randread", "randwrite"]
iodepths = ["1"]
iosizes = ["4K"]
num_threads = ["16", "32", "63"]

num_tasks = 1

tsk_set = task_set()
cpus_allowed = "64-127"

def main():
    fio_main(__file__, engines, iotypes, iodepths, iosizes, num_threads, cpus_allowed, "")
