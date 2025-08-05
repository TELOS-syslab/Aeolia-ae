#! /usr/bin/bash

cd ${LOCAL_AE_DIR}
sudo LD_PRELOAD=${LOCAL_AE_DIR}/LibStorage-Driver/apps/fio-plugin/ls_fio ${LOCAL_AE_DIR}/fio/fio ${LOCAL_AE_DIR}/workload/latency_aeolia.fio -lat_percentiles=1 --clat_percentiles=0 -output=data/latency_aeolia.json -output-format=json > data/metrics_device.dat


LATENCY=$(jq '.jobs[0].read.lat_ns.percentile."50.000000"' data/latency_aeolia.json)
echo "aeolia, $LATENCY" >> data/latency.csv