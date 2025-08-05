cd ${LOCAL_AE_DIR}/intus-kernel && git checkout main
sudo make menucofing  # please enable UINTR on the main menu, it's called User Interrupts (UINTR)
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install
