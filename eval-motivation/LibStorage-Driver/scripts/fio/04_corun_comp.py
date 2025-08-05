from common import psync, spdk, io_uring, bypassd, aeolia, aeolia_sched, \
    task_set, fio_main, base_other_test, comp

# engines = [psync, spdk, io_uring, bypassd, aeolia]
engines = [aeolia]
iotypes = ["randread"]
iodepths = ["1"]
iosizes = ["128K"]
num_threads = ["1"]

num_tasks = 1

tsk_set = task_set()
cpus_allowed = tsk_set["start"]

def main():
    fio_main(__file__, engines, iotypes, iodepths, iosizes, num_threads, cpus_allowed, comp)
