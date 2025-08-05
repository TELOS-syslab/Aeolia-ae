#! /usr/bin/bash

cd ${LOCAL_AE_DIR}/aeolia-kernel-uintr
cp /boot/config-$(uname -r) ./.config
sudo make menuconfig  # please enable UINTR on the main menu, it's called User Interrupts (UINTR)
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install
