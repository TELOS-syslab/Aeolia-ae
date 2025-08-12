#! /usr/bin/bash

cd ${LOCAL_AE_DIR}/LibStorage-Driver
source scripts/env.sh
bash scripts/setup.sh
sudo -E python scripts/execute.py
