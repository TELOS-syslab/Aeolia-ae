from common import psync, spdk, io_uring, bypassd, aeolia, aeolia_sched, \
    task_set, fio_main, base_other_test

# engines = [psync, spdk, io_uring, aeolia, aeolia_sched]
engines = [psync, spdk, io_uring, aeolia]
iotypes = ["randread", "randwrite"]
iodepths = ["1"]
iosizes = ["4K"]
num_threads = ["1", "2", "4", "8"]

num_tasks = 1

tsk_set = task_set()
cpus_allowed = tsk_set["start"]

def main():
    fio_main(__file__, engines, iotypes, iodepths, iosizes, num_threads, cpus_allowed, "")
