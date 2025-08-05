cd ${LOCAL_AE_DIR}/LibStorage-Driver-device-latency
sudo make

cd ${LOCAL_AE_DIR}
sudo LD_PRELOAD=${LOCAL_AE_DIR}/LibStorage-Driver-device-latency/apps/fio-plugin/ls_fio ${LOCAL_AE_DIR}/fio/fio ${LOCAL_AE_DIR}/workload/latency_device.fio -lat_percentiles=1 --clat_percentiles=0 -output=data/latency_device.json -output-format=json >> data/breakdown_metrics.dat