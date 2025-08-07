# Aeolia Motivation Artifact Evaluation

This folder will introduce how to evaluate the Figure 3, 4 and 5 in the motivation part of aeolia.

Since we patched io_uring and fio to simuate and analyze data, you need to change kernel during the ae. 

## Enviroment prepration

1. Clone this repo first :

```sh
git clone git@github.com:TELOS-syslab/LS.git Aeolia-ae
cd Aeolia-ae/eval-motivation
export LOCAL_AE_DIR=$(pwd)
echo "export LOCAL_AE_DIR=$(pwd)" >> ~/.bashrc
# change bashrc to other profiles you use.
```

## Evaluation for motivation

> Notice that we need to exlusive use io_uring.

### 1. Checkout to branch and build them

```sh
./scripts/build_1.sh

# please reboot to this new kernel, which will record timestamps and print them after io_uring processing
```

### 2. Run fio and capture data

#### 2.1 Get the breakdown time

```sh
./scripts/breakdown.sh
```

#### 2.2 Get the access latency

2.2.1 Latency with origin kernel

revert to the origin 6.12 kernel

```sh
./scripts/build_2.sh
# please reboot to this new kernel, which is the origin kernel that will show original performance
# after reboot, then
cd ${LOCAL_AE_DIR} && ./scripts/latency.sh
```
2.2.2 Latency with optimized iouring

```sh
./scripts/build_3.sh
# please reboot to this new kernel, which is the origin kernel that will show original performance
# after reboot, then
./scripts/iou_opt_latency.sh
```
2.2.3 Aeolia latency

Change kernel to Aeolia Kernel
```sh
# this shell will call a menuconfig, please enable the userinterrupt (UINTR) in the main menu
./scripts/build_4.sh
# please reboot to this new kernel, which is the origin kernel that will show original performance
# after reboot, then
./scripts/build_5.sh
./scripts/aeolia_latecy.sh

```
Init the driver submodule, and build it.
#### 2.3 Get the device latency

```sh

./scripts/device_latency.sh
```

### 3. Draw the figure

#### 3.1 Generate and collect data

Please change the `CPU_FREQ_GHZ = 1.9` in extract.py to your evaluate machine CPU frequency in GHz.

```sh
# This will generate a result with kernel layer overhead.
python3 scripts/extract.py data/breakdown_metrics.dat 
python3 scripts/clean_figure4_data.py
```
We draw figure3 by hand so there is no script for the figure. You can check the fig/fig3_data.csv for the data.

> We note that there is 100ns more for iouring callback to back to userspace, which we regard it as a common knowledge.

Now we have data/latency.csv for figure 4 and data/breakdown.csv for figure 5

#### Figure
```sh
python3 scripts/figure4.py
python3 scripts/figure5.py
```