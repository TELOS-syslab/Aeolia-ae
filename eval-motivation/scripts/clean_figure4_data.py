#!/usr/bin/env python3
"""
Data cleaning script for figure4 data
Converts latency.csv and dev_lat.log to the required CSV format
"""

import csv
import os

def read_latency_data():
    """Read latency data from latency.csv"""
    latency_data = {}
    with open('data/latency.csv', 'r') as f:
        for line in f:
            line = line.strip()
            if ',' in line:
                method, value = line.split(',')
                method = method.strip()
                try:
                    value = int(value.strip())
                    latency_data[method] = value
                except ValueError:
                    print(f"Warning: Invalid value '{value}' for method '{method}'")
    return latency_data

def read_device_latency():
    """Read device latency from dev_lat.log"""
    try:
        with open('data/dev_lat.log', 'r') as f:
            content = f.read().strip()
            if '.' in content:
                value = float(content)
                return int(round(value))
            return int(content)
    except (FileNotFoundError, ValueError) as e:
        print(f"Error reading dev_lat.log: {e}")
        return 0

def read_iou_breakdown():
    """Read iou_dfl breakdown data from iou_breakdown.csv"""
    try:
        with open('data/iou_breakdown.csv', 'r') as f:
            reader = csv.reader(f)
            header = next(reader)
            data = next(reader)
            
            breakdown = {}
            for i, value in enumerate(data):
                breakdown[header[i]] = int(float(value))
            return breakdown
    except (FileNotFoundError, ValueError, IndexError) as e:
        print(f"Error reading iou_breakdown.csv: {e}")
        return {}

def calculate_processing_time(total_time, device_time):
    """Calculate processing time as total time minus device time"""
    return max(0, total_time - device_time)

def generate_latency_csv():
    """Generate latency CSV with offset values"""
    latency_data = read_latency_data()
    
    # Define the order and offset values
    method_order = ['posix', 'iou_dfl', 'iou_opt', 'iou_poll', 'spdk', 'aeolia']
    offset_values = [0, 0.5, 1, 1.5, 2, 2.5]
    
    # Create output data
    output_data = []
    
    for i, method in enumerate(method_order):
        if method in latency_data:
            # Convert from nanoseconds to microseconds (divide by 1000)
            latency_us = latency_data[method] / 1000
            offset = offset_values[i] if i < len(offset_values) else 0
            
            output_data.append([method, f"{latency_us:.1f}", offset])
    
    # Write to CSV file
    output_file = 'data/latency_formatted.csv'
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        for row in output_data:
            writer.writerow(row)
    
    print(f"Latency data formatted and saved to {output_file}")
    
    # Print summary
    print("\nLatency Summary (in microseconds):")
    for row in output_data:
        print(f"{row[0]}: {row[1]} (offset: {row[2]})")

def generate_csv():
    """Generate the cleaned CSV data"""
    latency_data = read_latency_data()
    device_time = read_device_latency()
    iou_breakdown = read_iou_breakdown()

    print(iou_breakdown)
    
    # Define all methods
    methods = ['iou_dfl_flat', 'iou_poll_flat', 'spdk_flat', 'iou_dfl', 'iou_poll', 'spdk']
    
    # Prepare data for CSV
    categories = ['device', 'processing', 'kernel overhead', 'interrupt', 'schedule', 'others']
    
    # Create output data structure
    output_data = []
    
    for category in categories:
        row = {'Category': category}
        
        for method in methods:
            if method == 'iou_dfl_flat':
                # Use breakdown data for iou_dfl
                if category == 'device':
                    row[method] = iou_breakdown.get('device', device_time)
                elif category == 'processing':
                    row[method] = iou_breakdown.get(' processing', 0)
                elif category == 'kernel overhead':
                    row[method] = iou_breakdown.get(' k_overhead', 0)
                elif category == 'interrupt':
                    row[method] = iou_breakdown.get(' interrupt', 0)
                elif category == 'schedule':
                    row[method] = iou_breakdown.get(' scheduling', 0)
                elif category == 'others':
                    row[method] = iou_breakdown.get('others', 0)
            elif method == 'spdk_flat':
                # For spdk, only device and processing time
                if category == 'device':
                    row[method] = device_time
                elif category == 'processing':
                    total_time = latency_data.get("spdk", 0)
                    row[method] = calculate_processing_time(total_time, device_time)
                else:
                    row[method] = 0
            elif method == 'iou_poll_flat':
                # For other methods (iou_poll, posix, aeolia), only device and processing time
                if category == 'device':
                    row[method] = device_time
                elif category == 'processing':
                    total_time = latency_data.get("iou_poll", 0)
                    row[method] = calculate_processing_time(total_time, device_time)
                else:
                    row[method] = 0
            elif method == 'iou_dfl':
                # Use breakdown data for iou_dfl
                if category == 'device':
                    row[method] = iou_breakdown.get('device', device_time)
                elif category == 'processing':
                    row[method] = iou_breakdown.get(' processing', 0) + iou_breakdown.get('device', device_time)
                elif category == 'kernel overhead':
                    row[method] = iou_breakdown.get(' k_overhead', 0) + iou_breakdown.get(' processing', 0) + iou_breakdown.get('device', device_time)
                elif category == 'interrupt':
                    row[method] = iou_breakdown.get(' interrupt', 0) + iou_breakdown.get(' k_overhead', 0) + iou_breakdown.get(' processing', 0) + iou_breakdown.get('device', device_time)
                elif category == 'schedule':
                    row[method] = iou_breakdown.get(' scheduling', 0) + iou_breakdown.get(' interrupt', 0) + iou_breakdown.get(' k_overhead', 0) + iou_breakdown.get(' processing', 0) + iou_breakdown.get('device', device_time)
                elif category == 'others':
                    row[method] = iou_breakdown.get('others', 0) + iou_breakdown.get(' scheduling', 0) + iou_breakdown.get(' interrupt', 0) + iou_breakdown.get(' k_overhead', 0) + iou_breakdown.get(' processing', 0) + iou_breakdown.get('device', device_time)
            elif method == 'spdk':
                # For spdk, only device and processing time
                if category == 'device':
                    row[method] = device_time
                elif category == 'processing':
                    total_time = latency_data.get(method, 0)
                    row[method] = total_time
                else:
                    row[method] = 0
            elif method == 'iou_poll':
                # For other methods (iou_poll, posix, aeolia), only device and processing time
                if category == 'device':
                    row[method] = device_time
                elif category == 'processing':
                    total_time = latency_data.get(method, 0)
                    row[method] = calculate_processing_time(total_time, device_time)
                else:
                    row[method] = 0
            
        output_data.append(row)
    
    print(output_data)
    # Write to CSV file
    output_file = 'data/data_iou_spdk.csv'
    with open(output_file, 'w', newline='') as f:
        if output_data:
            fieldnames = ['Category'] + methods
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(output_data)
    
    print(f"Data cleaned and saved to {output_file}")
    
    # Print summary
    print("\nData Summary:")
    print(f"Device latency: {device_time}")
    for method in methods:
        if method in latency_data:
            total = latency_data[method]
            if method == 'iou_dfl':
                processing = iou_breakdown.get('processing', 0)
            else:
                processing = calculate_processing_time(total, device_time)
            print(f"{method}: total={total}, device={device_time}, processing={processing}")

if __name__ == "__main__":
    generate_csv()
    generate_latency_csv() 