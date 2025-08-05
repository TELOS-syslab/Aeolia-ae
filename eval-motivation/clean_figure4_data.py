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
            if ':' in line:
                method, value = line.split(':')
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
            return int(f.read().strip())
    except (FileNotFoundError, ValueError) as e:
        print(f"Error reading dev_lat.log: {e}")
        return 0

def calculate_processing_time(total_time, device_time):
    """Calculate processing time as total time minus device time"""
    return max(0, total_time - device_time)

def generate_csv():
    """Generate the cleaned CSV data"""
    latency_data = read_latency_data()
    device_time = read_device_latency()
    
    # Define the methods we want to process
    methods = ['iou_dfl', 'iou_poll', 'spdk']
    
    # Prepare data for CSV
    categories = ['device', 'processing', 'kernel overhead', 'interrupt', 'schedule', 'others']
    
    # Create output data structure
    output_data = []
    
    for category in categories:
        row = {'Category': category}
        
        for method in methods:
            if method in latency_data:
                if category == 'device':
                    row[method] = device_time
                elif category == 'processing':
                    processing_time = calculate_processing_time(latency_data[method], device_time)
                    row[method] = processing_time
                else:
                    row[method] = 0
            else:
                row[method] = 0
        
        output_data.append(row)
    
    # Write to CSV file
    output_file = 'data_iou_spdk.csv'
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
            processing = calculate_processing_time(total, device_time)
            print(f"{method}: total={total}, device={device_time}, processing={processing}")

if __name__ == "__main__":
    generate_csv() 