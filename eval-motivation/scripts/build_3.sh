#! /usr/bin/bash

cd ${LOCAL_AE_DIR}/fio-iou-opt
./configure
make -j$(nproc)

cd ${LOCAL_AE_DIR}/aeolia-kernel-iou_opt
cp /boot/config-$(uname -r) ./.config
sudo make olddefconfig
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install