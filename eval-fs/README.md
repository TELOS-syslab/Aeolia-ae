# Aeolia FS Artifact Evaluation

## 1. Evaluate single thread performance
Build the kernel

```sh
cd aeolia-kernel-uintr
cp /boot/config-$(uname -r) ./.config
sudo make olddefconfig

# please enable UINTR on the main menu, it's called User Interrupts (UINTR)
sudo make menuconfig
sudo make -j$(nproc)
sudo make modules_install -j$(nproc)
sudo make install
```

```sh
# build the aeolia system under kernel w/ uintr
sudo ./figure14.sh

```

## 2. Evaluate multi-thread performance

```sh
sudo ./figure15.sh
```

## 3. Evaluate metadata scalability (fxmark)

```sh
sudo ./figure16.sh

```

## 4. Evaluate Real-worlds (leveldb)

```sh
sudo ./table7.sh
```

## 5. Data Cleaning & Figure

```sh
sudo python3 clean_data.py

# Table 7 in table7/leveldb_performance.csv
sudo python3 plot_fs_single_thread.py # figure 14
sudo python3 plot_fs_multi_threads.py # figure 15
sudo python3 plot_fs_fxmark.py # figure 16
```