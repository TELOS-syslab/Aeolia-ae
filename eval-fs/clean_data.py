import pandas as pd
import os

def figure14():
    """Process figure14 data: 2M and 4K single thread FIO data"""
    
    # Create output directory
    output_dir = "fig1-fio-single-thread"
    os.makedirs(output_dir, exist_ok=True)
    
    # Process 2M data
    process_fio_data("data/fio_single_2M_data.csv", 
                     f"{output_dir}/2M_data.csv")
    
    # Process 4K data
    process_fio_data("data/fio_single_4K_data.csv", 
                     f"{output_dir}/4K_data.csv")
    
    # Process fxmark metadata
    process_fxmark_metadata(output_dir)

def process_fio_data(input_file, output_file):
    """Process FIO data and convert to GB/s format"""
    
    # Read data
    data = []
    with open(input_file, 'r') as f:
        for line in f:
            line = line.strip()
            if line:
                parts = [part.strip() for part in line.split(',')]
                if len(parts) == 3:
                    test_type, bytes_per_sec, fs = parts
                    # Convert bytes to GB/s
                    gb_per_sec = float(bytes_per_sec) / 1000000
                    data.append([test_type, gb_per_sec, fs])
    
    # Create DataFrame
    df = pd.DataFrame(data, columns=['type', 'GB/s', 'fs'])
    
    # Define sort order
    type_order = ['read', 'write']
    fs_order = ['ext4', 'f2fs', 'aeolia']
    
    # Sort data
    df['type'] = pd.Categorical(df['type'], categories=type_order, ordered=True)
    df['fs'] = pd.Categorical(df['fs'], categories=fs_order, ordered=True)
    df = df.sort_values(['type', 'fs'])
    
    # Save to CSV
    df.to_csv(output_file, index=False, header=False)
    print(f"Processed {input_file} -> {output_file}")

def process_fxmark_metadata(output_dir):
    """Process fxmark metadata files for single thread results"""
    
    # Define operation mapping
    op_mapping = {
        'MRPL': 'open',
        'MWCL': 'create', 
        'MWUL': 'delete'
    }
    
    # Define filesystems to process (excluding ufs)
    filesystems = ['ext4', 'f2fs', 'aeolia']
    
    data = []
    
    # Process each operation type
    for op_code, op_name in op_mapping.items():
        for fs in filesystems:
            filename = f"data/fxmark_{fs}_{op_code}.log"
            
            try:
                with open(filename, 'r') as f:
                    for line in f:
                        line = line.strip()
                        if line:
                            parts = line.split()
                            if len(parts) >= 3:
                                threads = int(parts[0])
                                # Only process single thread results
                                if threads == 1:
                                    # Third column is the data we need
                                    value = float(parts[3]) / (10**6)  # Convert to millions
                                    data.append([op_name, value, fs])
                                    break
            except FileNotFoundError:
                print(f"Warning: {filename} not found")
                continue
    
    # Create DataFrame and sort
    df = pd.DataFrame(data, columns=['operation', 'value', 'fs'])
    
    # Define sort order
    op_order = ['open', 'create', 'delete']
    fs_order = ['ext4', 'f2fs', 'aeolia']
    
    # Sort data
    df['operation'] = pd.Categorical(df['operation'], categories=op_order, ordered=True)
    df['fs'] = pd.Categorical(df['fs'], categories=fs_order, ordered=True)
    df = df.sort_values(['operation', 'fs'])
    
    # Save to CSV
    output_file = f"{output_dir}/meta_data.csv"
    df.to_csv(output_file, index=False, header=False)
    print(f"Processed fxmark metadata -> {output_file}")

def figure15():
    """Process figure15 data: multi-thread FIO data"""
    
    # Create output directory
    output_dir = "fig2-fio-multi-threads"
    os.makedirs(output_dir, exist_ok=True)
    
    # Define block sizes and types
    block_sizes = ['4K', '2M']
    types = ['read', 'write']
    
    # Process each combination
    for bs in block_sizes:
        for test_type in types:
            input_file = f"data/fio_multi_{bs}_{test_type}.csv"
            output_file = f"{output_dir}/fio_{bs}_{test_type}.csv"
            process_multi_fio_data(input_file, output_file)

def process_multi_fio_data(input_file, output_file):
    """Process multi-thread FIO data and create pivot table"""
    
    # Read data
    data = []
    with open(input_file, 'r') as f:
        for line in f:
            line = line.strip()
            if line:
                parts = [part.strip() for part in line.split(',')]
                if len(parts) == 3:
                    fs, threads, throughput = parts
                    # Convert to millions
                    throughput_millions = float(throughput) / (10**6)
                    data.append([fs, int(threads), throughput_millions])
    
    # Create DataFrame
    df = pd.DataFrame(data, columns=['fs', 'threads', 'throughput'])
    
    # Define expected thread counts and filesystems
    expected_threads = [1, 2, 4, 8, 16, 32, 64]
    expected_fs = ['ext4', 'f2fs', 'ufs', 'aeolia']
    
    # Create pivot table
    pivot_df = df.pivot(index='threads', columns='fs', values='throughput')
    
    # Reindex to include all expected thread counts
    pivot_df = pivot_df.reindex(expected_threads)
    
    # Reindex columns to include all expected filesystems
    pivot_df = pivot_df.reindex(columns=expected_fs)
    
    # Fill missing values with NaN (or you can use 0 or other default values)
    pivot_df = pivot_df.fillna(0)
    
    # Save to CSV with header
    pivot_df.to_csv(output_file, sep='\t')
    print(f"Processed {input_file} -> {output_file}")

def figure16():
    """Process figure16 data: fxmark multi-thread data"""
    
    # Create output directory
    output_dir = "fig3-fxmark"
    os.makedirs(output_dir, exist_ok=True)
    
    # Define all fxmark types
    fxmark_types = ['MRPL', 'MRPM', 'MRPH', 'MWCL', 'MWCM', 'MWUL', 'MWUM', 'MWRL', 'MWRM']
    
    # Process each type
    for fxmark_type in fxmark_types:
        output_file = f"{output_dir}/fxmark_{fxmark_type}.csv"
        process_fxmark_multi_data(fxmark_type, output_file)

def process_fxmark_multi_data(fxmark_type, output_file):
    """Process fxmark multi-thread data for a specific type"""
    
    # Define filesystems
    filesystems = ['ext4', 'f2fs', 'aeolia']
    
    # Collect data for all filesystems
    all_data = {}
    
    for fs in filesystems:
        filename = f"data/fxmark_{fs}_{fxmark_type}.log"
        
        try:
            with open(filename, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line:
                        parts = line.split()
                        if len(parts) >= 4:
                            threads = int(parts[0])
                            # Fourth column is the data we need
                            value = float(parts[3]) / (10**6)  # Convert to millions
                            
                            if threads not in all_data:
                                all_data[threads] = {}
                            all_data[threads][fs] = value
        except FileNotFoundError:
            print(f"Warning: {filename} not found")
            continue
    
    # Create DataFrame
    data_rows = []
    expected_threads = [1, 2, 4, 8, 16, 24, 32, 48, 56, 64]
    
    for threads in expected_threads:
        row = {'threads': threads}
        for fs in filesystems:
            row[fs] = all_data.get(threads, {}).get(fs, 0)
        data_rows.append(row)
    
    df = pd.DataFrame(data_rows)
    
    # Save to CSV with header
    df.to_csv(output_file, index=False)
    print(f"Processed fxmark {fxmark_type} -> {output_file}")

def table7():
    """Process table7 data: leveldb performance data"""
    
    # Create output directory
    output_dir = "table7"
    os.makedirs(output_dir, exist_ok=True)
    
    output_file = f"{output_dir}/leveldb_performance.csv"
    process_leveldb_data(output_file)

def process_leveldb_data(output_file):
    """Process leveldb data from all filesystems"""
    
    # Define filesystems
    filesystems = ['ext4', 'f2fs', 'aeolia']
    
    # Collect data for all filesystems
    all_data = {}
    
    for fs in filesystems:
        filename = f"data/leveldb_{fs}.log"
        
        try:
            with open(filename, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line:
                        # Parse line like "fill100K     :    5361.355 ops/s;  511.4 MB/s (1000 ops)"
                        parts = line.split(':')
                        if len(parts) >= 2:
                            test_type = parts[0].strip()
                            # Extract the ops/s value
                            ops_part = parts[1].split('ops/s')[0].strip()
                            try:
                                ops_value = float(ops_part)
                                # Convert to thousands
                                ops_thousands = ops_value / 1000
                                
                                if test_type not in all_data:
                                    all_data[test_type] = {}
                                all_data[test_type][fs] = ops_thousands
                            except ValueError:
                                continue
        except FileNotFoundError:
            print(f"Warning: {filename} not found")
            continue
    
    # Create DataFrame
    data_rows = []
    for test_type in all_data.keys():
        row = {'TP': test_type}
        for fs in filesystems:
            row[fs] = all_data[test_type].get(fs, 0)
        data_rows.append(row)
    
    df = pd.DataFrame(data_rows)
    
    # Save to CSV with header
    df.to_csv(output_file, index=False)
    print(f"Processed leveldb data -> {output_file}")

def figure18():
    """Process figure18 data: filebench performance data"""
    
    # Create output directory
    output_dir = "fig4-filebench"
    os.makedirs(output_dir, exist_ok=True)
    
    # Define filebench types
    filebench_types = ['varmail', 'webserver', 'fileserver', 'webproxy']
    
    # Process each type
    for fb_type in filebench_types:
        input_file = f"data/filebench_{fb_type}.log"
        output_file = f"{output_dir}/filebench_{fb_type}.csv"
        process_filebench_data(input_file, output_file)

def process_filebench_data(input_file, output_file):
    """Process filebench data from a specific log file"""
    
    # Define filesystems
    filesystems = ['ext4', 'f2fs', 'aeolia']
    
    # Collect data for all filesystems
    all_data = {}
    
    try:
        with open(input_file, 'r') as f:
            lines = f.readlines()
            
            i = 0
            while i < len(lines):
                line = lines[i].strip()
                if line:
                    # Parse first line: "ext4 1" or "f2fs 2" etc.
                    parts = line.split()
                    if len(parts) >= 2:
                        fs = parts[0]
                        threads = int(parts[1])
                        
                        # Read next line for IO Summary
                        if i + 1 < len(lines):
                            next_line = lines[i + 1].strip()
                            if 'IO Summary:' in next_line:
                                # Extract ops/s value
                                # Format: "8.186: IO Summary: 233917 ops 46779.545 ops/s ..."
                                summary_parts = next_line.split('ops/s')
                                if len(summary_parts) >= 1:
                                    ops_part = summary_parts[0].split()[-1]
                                    try:
                                        ops_value = float(ops_part)
                                        
                                        if threads not in all_data:
                                            all_data[threads] = {}
                                        all_data[threads][fs] = ops_value
                                    except ValueError:
                                        pass
                        i += 2  # Skip both lines
                    else:
                        i += 1
                else:
                    i += 1
    except FileNotFoundError:
        print(f"Warning: {input_file} not found")
        return
    
    # Create DataFrame
    data_rows = []
    expected_threads = [1, 2, 4, 8, 16, 32, 64]
    
    for threads in expected_threads:
        row = {'threads': threads}
        for fs in filesystems:
            row[fs] = all_data.get(threads, {}).get(fs, 0)
        data_rows.append(row)
    
    df = pd.DataFrame(data_rows)
    
    # Save to CSV with header
    df.to_csv(output_file, index=False)
    print(f"Processed {input_file} -> {output_file}")

if __name__ == "__main__":
    # figure14()
    # figure15()
    figure16()
    # table7()
    figure18()
