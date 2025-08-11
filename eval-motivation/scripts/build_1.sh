#! /usr/bin/bash

cd ${LOCAL_AE_DIR}/fio
./configure
make -j$(nproc)

cd ${LOCAL_AE_DIR}/spdk
sudo ./scripts/pkgdep.sh
./configure --with-fio=${LOCAL_AE_DIR}/fio
make -j$(nproc)

cd ${LOCAL_AE_DIR}/aeolia_kernel-interrupt_breakdown
cp /boot/config-$(uname -r) ./.config
sudo make olddefconfig
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install
