# Aeolia Driver Artifact Evaluation

This folder will introduce how to evaluate the Figure 6, 10, 11, 12 and 13 in the driver part of aeolia.

You need to change kernel during the ae. 

## Enviroment prepration

1. Clone this repo first :

```sh
git clone git@github.com:TELOS-syslab/LS.git Aeolia-ae
cd Aeolia-ae/eval-driver
git checkout ae
export LOCAL_AE_DIR=$(pwd)
echo "export LOCAL_AE_DIR=$(pwd)" >> ~/.bashrc
# change bashrc to other profiles you use.
```

## Driver evaluation

### 1. Evaluation

#### 1.1 Revise environment variables

Note that the NVMe-related variables in scripts/env.sh should be replaced by your setup. 

#### 1.2 Get the raw data

1.2.1 Origin kernel

revert to the origin 6.12 kernel

```sh
bash ./scripts/build_1.sh && sudo reboot

# after reboot, then

bash ./scripts/eval-aeolia.sh    # this evaluation will take around 1.5 hours
```

1.2.2 Aeolia

Change kernel to Aeolia Kernel
```sh
bash ./scripts/build_2.sh && sudo reboot

# after reboot, then

bash ./scripts/eval-aeolia.sh     # this evaluation will take around 45 minutes

```

### 2. Draw figures

```sh
bash ./scripts/fig.sh
```

Generated figures could be found in directory ${LOCAL_AE_DIR}/fig/
