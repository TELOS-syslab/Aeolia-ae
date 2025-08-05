cd ${LOCAL_AE_DIR}/fio && git checkout master # this is the original fio 3.38
./configure
make -j$(nproc)

cd ${LOCAL_AE_DIR}/spdk && git checkout v24.09
sudo ./scripts/pkgdep.sh
git submodule update --init
./configure --with-fio=${LOCAL_AE_DIR}/fio
make -j$(nproc)

cd ${LOCAL_AE_DIR}/intus-kernel && git checkout interrupt_breakdown
cp /boot/config-$(uname -r) ./.config
sudo make oldconfig
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install