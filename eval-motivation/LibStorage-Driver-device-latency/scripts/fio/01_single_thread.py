from common import psync, spdk, io_uring, bypassd, aeolia, aeolia_sched, \
    task_set, fio_main

# engines = [psync, spdk, io_uring, bypassd, aeolia]
engines = [psync, spdk, io_uring, aeolia]
iotypes = ["randread", "randwrite"]
iodepths = ["1"]
# iosizes = ["4K", "16K", "64K", "256K", "1M", "2M"]
iosizes = ["512B", "1K", "2K", "4K", "8K", "16K", "32K", "64K", "128K", "256K", "512K"]
num_threads = ["1"]

num_tasks = 1

tsk_set = task_set()
cpus_allowed = tsk_set["start"]

def main():
    fio_main(__file__, engines, iotypes, iodepths, iosizes, num_threads, cpus_allowed, "")
