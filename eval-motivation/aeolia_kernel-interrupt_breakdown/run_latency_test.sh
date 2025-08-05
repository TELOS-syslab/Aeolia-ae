#!/bin/bash

# 执行fio latency测试
fio latency_posix.fio --output-format=json --output=latency_posix.json

# 提取p50延迟数据 (单位: 微秒)
jq '.jobs[0].read.clat.percentile["50.000000"]' latency_posix.json 