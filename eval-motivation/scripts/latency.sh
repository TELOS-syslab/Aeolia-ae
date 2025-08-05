#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 <nvme_pci_address>"
    echo "Example: $0 0000:c8:00.0"
    exit 1
fi

sudo ${LOCAL_AE_DIR}/fio/fio ${LOCAL_AE_DIR}/workload/latency_posix.fio -lat_percentiles=1 --clat_percentiles=0 -output=data/latency_posix.json -output-format=json

LATENCY=$(jq '.jobs[0].read.lat_ns.percentile."50.000000"' data/latency_posix.json)
echo "posix, $LATENCY" > data/latency.csv

sudo ${LOCAL_AE_DIR}/fio/fio ${LOCAL_AE_DIR}/workload/latency_iou_dfl.fio -lat_percentiles=1 --clat_percentiles=0 -output=data/latency_iou_dfl.json -output-format=json

LATENCY=$(jq '.jobs[0].read.lat_ns.percentile."50.000000"' data/latency_iou_dfl.json)
echo "iou_dfl, $LATENCY" >> data/latency.csv

sudo ${LOCAL_AE_DIR}/fio/fio ${LOCAL_AE_DIR}/workload/latency_iou_poll.fio -lat_percentiles=1 --clat_percentiles=0 -output=data/latency_iou_poll.json -output-format=json

LATENCY=$(jq '.jobs[0].read.lat_ns.percentile."50.000000"' data/latency_iou_poll.json)
echo "iou_poll, $LATENCY" >> data/latency.csv

sudo PCI_ALLOWED=$1 bash ${LOCAL_AE_DIR}/spdk/scripts/setup.sh config

NVME_PCI_ADDR=$(echo $1 | sed 's/:/./g')
sudo LD_PRELOAD=${LOCAL_AE_DIR}/spdk/build/fio/spdk_nvme ${LOCAL_AE_DIR}/fio/fio ${LOCAL_AE_DIR}/workload/latency_spdk.fio -lat_percentiles=1 --clat_percentiles=0 -output=data/latency_spdk.json -output-format=json --filename="trtype=PCIe traddr=$NVME_PCI_ADDR"

LATENCY=$(jq '.jobs[0].read.lat_ns.percentile."50.000000"' data/latency_spdk.json)
echo "spdk, $LATENCY" >> data/latency.csv

sudo bash ${LOCAL_AE_DIR}/spdk/scripts/setup.sh reset
