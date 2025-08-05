#! /usr/bin/bash

cd ${LOCAL_AE_DIR}/fio
sudo make -j$(nproc)

cd ${LOCAL_AE_DIR}/LibStorage-Driver
sudo make