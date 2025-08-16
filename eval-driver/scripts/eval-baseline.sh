#! /usr/bin/bash

sudo dd if=/dev/zero of=/dev/nvme0n1 bs=1M count=100
cd ${LOCAL_AE_DIR}/LibStorage-Driver
source scripts/env.sh
bash scripts/setup.sh
sudo -E python scripts/execute.py -b
