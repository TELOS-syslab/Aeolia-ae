
cd ${LOCAL_AE_DIR}/fio && git checkout fio_changed_header
sudo make -j$(nproc)

cd ${LOCAL_AE_DIR}/LibStorage-Driver && git checkout driver_eval
sudo make