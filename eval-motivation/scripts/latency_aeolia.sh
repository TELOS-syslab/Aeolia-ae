
sudo ${LOCAL_AE_DIR}/aeolia-fio/fio ${LOCAL_AE_DIR}/workload/latency_iou_dfl.fio -lat_percentiles=1 --clat_percentiles=0 -output=data/latency_posix.json -output-format=json
