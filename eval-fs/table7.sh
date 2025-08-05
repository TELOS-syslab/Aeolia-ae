#!/bin/bash
make_aeolia_leveldb() {
    echo "Making Aeolia filesystem..."
    cd leveldb-sufs
    sudo bash compile.sh && cd ..
}

make_leveldb() {
    echo "Making leveldb filesystem..."
    cd leveldb
    sudo bash compile.sh && cd ..
}
# Initialize functions for different filesystems
initialize_aeolia() {
    echo "Initializing Aeolia filesystem..."
    sudo make
}

initialize_ext4() {
    echo "Initializing ext4 filesystem..."
    
    # Unmount if mounted
    sudo umount /dev/nvme0n1 2>/dev/null || true
    
    # Format as ext4
    sudo mkfs.ext4 -F /dev/nvme0n1
    
    # Create mount point if not exists
    sudo mkdir -p /mnt/eval_ext4
    
    # Mount
    sudo mount /dev/nvme0n1 /mnt/eval_ext4
}

initialize_f2fs() {
    echo "Initializing f2fs filesystem..."
    
    # Unmount if mounted
    sudo umount /dev/nvme0n1 2>/dev/null || true
    
    # Format as f2fs
    sudo mkfs.f2fs -f /dev/nvme0n1
    
    # Create mount point if not exists
    sudo mkdir -p /mnt/eval_f2fs
    
    # Mount
    sudo mount /dev/nvme0n1 /mnt/eval_f2fs
}

evaluate_aeolia() {
    echo "Evaluating Aeolia filesystem..."
    
    # Benchmarks
    benchmarks=(fill100K fillseq fillsync fillrandom readrandom deleterandom)
    
    for benchmark in "${benchmarks[@]}"; do
        echo "Running Aeolia benchmark: $benchmark"
        initialize_aeolia
        # Special handling for deleterandom
        if [ "$benchmark" = "deleterandom" ]; then
            # First run fillseq
            sudo taskset -c 1-4 db_bench_sufs --db=/sufs --benchmarks=fillseq,deleterandom --threads=1 | grep $benchmark >> data/leveldb_aeolia.log
        else
            sudo taskset -c 1-4 db_bench_sufs --db=/sufs --benchmarks="$benchmark" --threads=1 | grep $benchmark >> data/leveldb_aeolia.log
        fi
        
        echo "Completed Aeolia benchmark: $benchmark"
        echo "------------------------"
    done
}

evaluate_ext4() {
    echo "Evaluating ext4 filesystem..."
    
    # Benchmarks
    benchmarks=(fill100K fillseq fillsync fillrandom readrandom deleterandom)
    
    for benchmark in "${benchmarks[@]}"; do
        echo "Running ext4 benchmark: $benchmark"
        initialize_ext4
        # Special handling for deleterandom
        if [ "$benchmark" = "deleterandom" ]; then
            # First run fillseq
            sudo taskset -c 1-4 db_bench --db=/mnt/eval_ext4 --benchmarks=fillseq,deleterandom --threads=1 | grep $benchmark >> data/leveldb_ext4.log
        else
            sudo taskset -c 1-4 db_bench --db=/mnt/eval_ext4 --benchmarks="$benchmark" --threads=1 | grep $benchmark >> data/leveldb_ext4.log
        fi
        
        echo "Completed ext4 benchmark: $benchmark"
        echo "------------------------"
    done
}

evaluate_f2fs() {
    echo "Evaluating f2fs filesystem..."
    
    # Benchmarks
    benchmarks=(fill100K fillseq fillsync fillrandom readrandom deleterandom)
    
    for benchmark in "${benchmarks[@]}"; do
        echo "Running f2fs benchmark: $benchmark"
        initialize_f2fs
        # Special handling for deleterandom
        if [ "$benchmark" = "deleterandom" ]; then
            # First run fillseq
            sudo taskset -c 1-4 db_bench --db=/mnt/eval_f2fs --benchmarks=fillseq,deleterandom --threads=1 | grep $benchmark >> data/leveldb_f2fs.log
        else
            sudo taskset -c 1-4 db_bench --db=/mnt/eval_f2fs --benchmarks="$benchmark" --threads=1 | grep $benchmark >> data/leveldb_f2fs.log
        fi
        
        echo "Completed f2fs benchmark: $benchmark"
        echo "------------------------"
    done
}

# Main execution
main() {
    echo "Starting Table 7 evaluation..."
    
    mkdir -p data
    make_aeolia_leveldb
    evaluate_aeolia
    
    make_leveldb
    evaluate_ext4
    evaluate_f2fs
    
    echo "Table 7 evaluation completed."
}

# Run main function
main "$@"
