#! /usr/bin/bash

cd ${LOCAL_AE_DIR}/aeolia-kernel-orig_6.12
cp /boot/config-$(uname -r) ./.config
sudo make olddefconfig
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install