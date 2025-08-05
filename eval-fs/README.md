# Aeolia FS Artifact Evaluation

## 1. Evaluate single thread performance

```sh
# build the aeolia system under kernel w/ uintr
sudo sh figure14.sh

```

## 2. Evaluate multi-thread performance

```sh
sudo sh figure15.sh
```

## 3. Evaluate metadata scalability (fxmark)

```sh
sudo sh figure16.sh

```

## 4. Evaluate filebench

```sh
sudo sh figure18.sh
```

## 5. Evaluate Real-worlds (leveldb)

```sh
sudo sh table7.sh
```

## 6. Data Cleaning & Figure

```sh
sudo python3 clean_data.py

# Table 7 in table7/leveldb_performance.csv
sudo python3 plot_fs_single_thread.py # figure 14
sudo python3 plot_fs_multi_threads.py # figure 15
sudo python3 plot_fs_fxmark.py # figure 16
sudo python3 plot_fs_filebench # figure 18
```