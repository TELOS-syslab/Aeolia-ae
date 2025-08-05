sudo ${LOCAL_AE_DIR}/fio-iou-opt/fio ${LOCAL_AE_DIR}/workload/latency_iou_opt.fio -lat_percentiles=1 --clat_percentiles=0 -output=data/latency_iou_opt.json -output-format=json

LATENCY=$(jq '.jobs[0].read.lat_ns.percentile."50.000000"' data/latency_iou_opt.json)
echo "iou_opt, $LATENCY" >> data/latency.csv