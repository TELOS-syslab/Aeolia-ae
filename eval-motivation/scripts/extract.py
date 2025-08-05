#!/usr/bin/env python3

import sys
import re

# CPU frequency in GHz
CPU_FREQ_GHZ = 1.9

def tsc_to_ns(tsc):
    """Convert TSC cycles to nanoseconds"""
    return (tsc / CPU_FREQ_GHZ)

def process_line(line):
    """Process a single line of BREAKDOWN_METRICS data"""
    # Extract timestamp and metrics
    match = re.search(r'\[BREAKDOWN_METRICS\] : (.+)', line)
    if not match:
        return None
    
    # Parse the metrics values
    metrics_str = match.group(1).strip()
    metrics = [int(x) for x in metrics_str.split()]
    
    # Check if we have enough data (should be 10 values)
    if len(metrics) != 17:
        return None
    
    # Check if any required metric is 0
    for i in range(len(metrics)):
        if metrics[i] == 0:
            return None
    
    # Calculate time differences
    io_submit = tsc_to_ns(metrics[1] - metrics[0])
    cs_to_idle = tsc_to_ns(metrics[2] - metrics[1])
    device_interrupt_time = tsc_to_ns(metrics[3] - metrics[1])
    io_complete = tsc_to_ns(metrics[4] - metrics[3])
    wakeup_time = tsc_to_ns(metrics[5] - metrics[4])
    update_stat = tsc_to_ns(metrics[7] - metrics[16])
    schedule_context_switch = tsc_to_ns(metrics[8] - metrics[7])
    iou_callback = tsc_to_ns(metrics[9] - metrics[8])
    
    k_io_submit = tsc_to_ns(metrics[13] - metrics[0])
    k_io_complete = tsc_to_ns(metrics[4] - metrics[14])
    return {
        'io_submit': io_submit,
        'cs_to_idle': cs_to_idle,
        'device_interrupt_time': device_interrupt_time,
        'io_complete': io_complete,
        'wakeup_time': wakeup_time,
        'update_stat': update_stat,
        'schedule_context_switch': schedule_context_switch,
        'iou_callback': iou_callback,
        'k_overhead_submit': k_io_submit,
        'k_overhead_complete': k_io_complete
    }

def process_device_latency(line):
    """Process a single line of RECORD DEVICE LATENCY data"""
    match = re.search(r'\[RECORD DEVICE LATENCY\] : (\d+)', line)
    if not match:
        return None
    
    device_latency_tsc = int(match.group(1))
    device_latency_ns = tsc_to_ns(device_latency_tsc)
    
    return device_latency_ns

def get_median(values):
    """Calculate median of a list of values"""
    sorted_values = sorted(values)
    n = len(sorted_values)
    if n % 2 == 0:
        return (sorted_values[n//2 - 1] + sorted_values[n//2]) / 2
    else:
        return sorted_values[n//2]

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 extract.py <input_file>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    valid_results = []
    device_latency_values = []
    
    try:
        with open(input_file, 'r') as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                
                # Process BREAKDOWN_METRICS data
                if '[BREAKDOWN_METRICS]' in line:
                    result = process_line(line)
                    if result:
                        valid_results.append(result)
                        print(f"Line {line_num}:")
                        print(f"  IO Submit: {result['io_submit']:.2f} ns")
                        print(f"  CS to Idle: {result['cs_to_idle']:.2f} ns")
                        print(f"  Device Time + Interrupt Time: {result['device_interrupt_time']:.2f} ns")
                        print(f"  IO Complete: {result['io_complete']:.2f} ns")
                        print(f"  Wakeup time: {result['wakeup_time']:.2f} ns")
                        print(f"  Update Stat.: {result['update_stat']:.2f} ns")
                        print(f"  Schedule + Context Switch: {result['schedule_context_switch']:.2f} ns")
                        print(f"  IOU Callback: {result['iou_callback']:.2f} ns")
                        print(f"  K Overhead Submit: {result['k_overhead_submit']:.2f} ns")
                        print(f"  K Overhead Complete: {result['k_overhead_complete']:.2f} ns")
                        print()
                    else:
                        # print(f"Line {line_num}: Invalid data (contains 0 values), skipped")
                        # print()
                        pass
                
                # Process RECORD DEVICE LATENCY data
                elif 'RECORD DEVICE LATENCY' in line:
                    device_latency = process_device_latency(line)
                    if device_latency is not None:
                        device_latency_values.append(device_latency)
                        # print(f"Line {line_num}: Device Latency: {device_latency:.2f} ns")
                        # print()
    
    except FileNotFoundError:
        print(f"Error: File '{input_file}' not found")
        sys.exit(1)
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)
    
    if not valid_results and not device_latency_values:
        print("No valid data found")
        return
    
    # Calculate P50 medians for BREAKDOWN_METRICS
    if valid_results:
        num_valid = len(valid_results)
        # print(f"Total valid BREAKDOWN_METRICS lines: {num_valid}")
        # print("\nBREAKDOWN_METRICS P50 Medians:")
        
        # Extract all values for each metric
        io_submit_values = [r['io_submit'] for r in valid_results]
        cs_to_idle_values = [r['cs_to_idle'] for r in valid_results]
        device_interrupt_time_values = [r['device_interrupt_time'] for r in valid_results]
        io_complete_values = [r['io_complete'] for r in valid_results]
        wakeup_time_values = [r['wakeup_time'] for r in valid_results]
        update_stat_values = [r['update_stat'] for r in valid_results]
        schedule_context_switch_values = [r['schedule_context_switch'] for r in valid_results]
        iou_callback_values = [r['iou_callback'] for r in valid_results]
        k_io_complete_values = [r['k_overhead_complete'] for r in valid_results]
        k_io_submit_values = [r['k_overhead_submit'] for r in valid_results]
        
        median_io_submit = get_median(io_submit_values)
        median_cs_to_idle = get_median(cs_to_idle_values)
        median_device_interrupt_time = get_median(device_interrupt_time_values)
        median_io_complete = get_median(io_complete_values)
        median_wakeup_time = get_median(wakeup_time_values)
        median_update_stat = get_median(update_stat_values)
        median_schedule_context_switch = get_median(schedule_context_switch_values)
        median_iou_callback = get_median(iou_callback_values)
        median_k_io_complete = get_median(k_io_complete_values)
        median_k_io_submit = get_median(k_io_submit_values)
    
    # Calculate P50 median for device latency
    if device_latency_values:
        num_device_latency = len(device_latency_values)
        # print(f"\nTotal valid RECORD DEVICE LATENCY lines: {num_device_latency}")
        # print("\nDevice Latency P50 Median:")
        
        median_device_latency = get_median(device_latency_values)
    
    ## Fig3
    with open("fig/fig3_data.csv", "w") as f:
        f.write("IO Submit, CS to Idle, Device, Interrupt, IO Complete, Wake Up, Update Stat., Schedule, IOU Callback\n")
        f.write(f"{median_io_submit-median_k_io_submit}, {median_cs_to_idle}, {median_device_latency}, {median_device_interrupt_time - median_device_latency}, {median_io_complete - median_k_io_complete}, {median_wakeup_time}, {median_update_stat}, {median_schedule_context_switch}, {median_iou_callback}")
    
    with open("data/dev_lat.log", "w") as f:
        f.write(f"{median_device_latency}")

    total = median_io_submit + median_device_interrupt_time + median_io_complete + median_wakeup_time + median_update_stat + median_schedule_context_switch + median_iou_callback
    
    with open("data/latency.csv", "r") as f:
        io_uring_dft = None
        for line in f:
            line = line.strip()
            if line.startswith("iou_dfl"):
                parts = line.split(",")
                iou_dft = int(parts[1].strip())
        if io_uring_dft is None:
            io_uring_dft = 0
    with open("data/iou_breakdown.csv", "w") as f:
        f.write("others, scheduling, interrupt, k_overhead, processing, device\n")
        f.write(f"{iou_dft - total}, {median_schedule_context_switch+median_update_stat+median_wakeup_time}, {median_device_interrupt_time-median_device_latency}, {median_k_io_complete+median_k_io_submit}, {median_io_complete - median_k_io_complete + median_io_submit-median_k_io_submit}, {median_device_latency}")


if __name__ == "__main__":
    main()
