from common import psync, spdk, io_uring, bypassd, aeolia, aeolia_sched, \
    task_set, fio_main, base_other_test

# engines = [psync, spdk, io_uring, aeolia, aeolia_sched]
engines = [psync, spdk, io_uring, aeolia]
iotypes = ["randread", "randwrite"]
iodepths = ["1"]
iosizes = ["4K"]
num_threads = ["1", "2", "4", "8"]

num_tasks = 2

tsk_set = task_set()
cpus_allowed = f"{tsk_set["start"]}-{tsk_set["start"] + 3}"

tapp = base_other_test.format(task_name="tapp", iodepth="16", iosize="64K", numjobs="1", cpus_allowed=cpus_allowed, nice=0)
# tapp += "\ntype=0\nintr=1"

def main():
    fio_main(__file__, engines, iotypes, iodepths, iosizes, num_threads, cpus_allowed, [tapp])
