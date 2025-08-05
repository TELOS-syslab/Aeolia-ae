# Aeolia Motivation Artifact Evaluation

This folder will introduce how to evaluate the Figure 3, 4 and 5 in the motivation part of aeolia.

Since we patched io_uring and fio to simuate and analyze data, you need to change kernel during the ae. 

## Enviroment prepration

1. Clone this repo and checkout to ae branch first :

```sh
git clone https://github.com/TELOS-syslab/LS.git
cd LS && git checkout ae
export LOCAL_AE_DIR=$(pwd)
echo "LOCAL_AE_DIR=$(pwd)" >> ~/.bashrc
# change bashrc to other profiles you use.
```

2. Clone the aeolia kernel, fio and spdk then:

```sh
git clone git@github.com:TELOS-syslab/intus-kernel.git 
git clone git@github.com:TELOS-syslab/aeolia_fio.git fio
git clone git@github.com:spdk/spdk.git
git clone git@github.com:TELOS-syslab/LibStorage-Driver.git
```

## Evaluation for motivation

> Notice that we need to exlusive use io_uring.

### 1. Checkout to branch and build them

```sh
sudo bash build_1.sh

# please reboot to this new kernel, which will record timestamps and print them after io_uring processing
```

### 2. Run fio and capture data

#### 2.1 Get the breakdown time

```sh
sudo bash breakdown.sh
```

You can now get data for figure3. 
Note that the IOU Callback part actually need an extra kernel cross to back to userspace, which cost about 100 ns.

#### 2.2 Get the access latency

2.2.1 Latency with origin kernel

revert to the origin 6.12 kernel

```sh
sudo bash build_2.sh
# please reboot to this new kernel, which is the origin kernel that will show original performance
# after reboot, then
cd ${LOCAL_AE_DIR} && ./scripts/latency.sh
```

2.2.2 iou_opt (interrupt and no scheduling) [WIP]

> This will coming soon, since the iouring changed in kernel a lot compare to when we do it.

```sh
cd ${LOCAL_AE_DIR}/fio && git checkout iou_opt
sudo make -j$(nproc)

cd ${LOCAL_AE_DIR}/intus-kernel && git checkout io_uring_opt
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install
# please reboot to this new kernel, which is the origin kernel that will show original performance

# after reboot, then

cd ${LOCAL_AE_DIR}

sudo ./fio/fio workloads/io_uring_opt.fio -lat_percentiles=1 --clat_percentiles=0 -output=data/latency_iou_opt.json -output-format=json
LATENCY=$(jq '.jobs[0].read.lat_ns.percentile."50.000000"' data/latency_iou_poll.json)
echo "iou_opt, $LATENCY" >> data/latency.csv
```
2.2.3 Aeolia latency

Change kernel to Aeolia Kernel
```sh
sudo bash build_4.sh
# please reboot to this new kernel, which is the origin kernel that will show original performance
# after reboot, then
sudo bash build_5.sh
sudo bash aeolia_latecy.sh

```
Init the driver submodule, and build it.
#### 2.3 Get the device latency

```sh

sudo bash device_latency.sh
```

### 3. Draw the figure

#### 3.1 Generate and collect data

```sh


# This will generate a result with kernel layer overhead.
python3 scripts/extract.py data/breakdown_metrics.dat 
python3 clean_figure4_data.py
```
We draw figure3 by hand so there is no script for the figure.
Now we have data/latency.csv for figure 4 and data/breakdown.csv for figure 5

#### Figure
```sh
python3 plot_iou_spdk.py
```