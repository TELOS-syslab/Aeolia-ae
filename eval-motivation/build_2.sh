cd ${LOCAL_AE_DIR}/intus-kernel && git checkout orig_6.12
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install