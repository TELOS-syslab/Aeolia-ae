#! /usr/bin/bash

cd ${LOCAL_AE_DIR}
mkdir data

sudo ./fio/fio ./workload/interrupt_breakdown.fio
sudo dmesg | grep BREAKDOWN > data/breakdown_metrics.dat
