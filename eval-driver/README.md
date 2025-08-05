# Aeolia Driver Artifact Evaluation

This folder will introduce how to evaluate the Figure 6, 10, 11, 12 and 13 in the driver part of aeolia.

You need to change kernel during the ae. 

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

## Driver evaluation

### 1. Preliminary

```sh
cd ${LOCAL_AE_DIR}/fio && git checkout master # this is the original fio 3.38
./configure
make -j$(nproc)

cd ${LOCAL_AE_DIR}/spdk && git checkout v24.09
sudo ./scripts/pkgdep.sh
git submodule update --init
./configure --with-fio=${LOCAL_AE_DIR}/fio
make -j$(nproc)

cd ${LOCAL_AE_DIR}/LibStorage-Driver && git checkout eval
git submodule init tools/meson && git submodule update tools/meson

```

### 2. Evaluation

#### 2.1 Revise environment variables

Note that the NVMe-related variables in scripts/env.sh should be replaced by your setup. 
Also revise the spdk root directory in scripts/env.sh.

#### 2.2 Get the raw data

2.2.1 Origin kernel

revert to the origin 6.12 kernel

```sh
cd ${LOCAL_AE_DIR}/intus-kernel && git checkout orig_6.12
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install
# please reboot to this new kernel

# after reboot, then

cd ${LOCAL_AE_DIR}/LibStorage-Driver
source scripts/env.sh
sudo -E python scripts/execute.py -b
```

2.2.2 Aeolia

Change kernel to Aeolia Kernel
```sh
cd ${LOCAL_AE_DIR}/intus-kernel && git checkout main
sudo make menucofing  # please enable UINTR on the main menu, it's called User Interrupts (UINTR)
                      # alos enable SCHED_CLASS_EXT (location: General setup -> Extensible Scheduling Class)
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install

# please reboot to this new kernel

# after reboot, then

cd ${LOCAL_AE_DIR}/LibStorage-Driver && git checkout eval
source scripts/env.sh
bash scripts/setup.sh
sudo -E python scripts/execute.py

```

### 3. Draw the figure

#### 3.1 Collect data

```sh
python scripts/fio/extract.py
```

#### 3.2 Draw figures

```sh
python scripts/fig/fig10.py
```

You can run the python scripts in scripts/fig/ to generate associated figures posted in the submission.
