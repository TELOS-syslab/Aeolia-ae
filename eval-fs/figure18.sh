#!/bin/bash

# Initialize functions for different filesystems
make_filebench() {
    
    # Use Makefile with sufs support
    sudo make
    cd filebench
    sudo bash compile.sh
    cd ..
}

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
    
    tests=(fileserver webserver varmail webproxy)
    nthreads=(1 2 4 8 16 32 64)
    for test in "${tests[@]}"; do
        for nthread in "${nthreads[@]}"; do
            echo "Running aeolia test: $test $nthread"
            initialize_aeolia
            sed -e "s/^set \$nthreads=.*/set \$nthreads=$nthread/" -e "s|^set \$dir=.*|set \$dir=/sufs|" ./filebench/workloads/$test.f > ./filebench/workloads/${test}_tmp.f
            echo "aeolia ${nthread} " >> data/filebench_${test}.log
            sudo filebench-sufs -f ./filebench/workloads/${test}_tmp.f | grep "IO Summary" >> data/filebench_${test}.log
        done
    done
}

evaluate_ext4() {

    echo "Evaluating ext4 filesystem..."
    
    tests=(fileserver webserver varmail webproxy)
    nthreads=(1 2 4 8 16 32 64)
    for test in "${tests[@]}"; do
        for nthread in "${nthreads[@]}"; do
            echo "Running ext4 test: $test $nthread"
            initialize_ext4
            sed -e "s/^set \$nthreads=.*/set \$nthreads=$nthread/" -e "s|^set \$dir=.*|set \$dir=/mnt/eval_ext4|" ./filebench/workloads/$test.f > ./filebench/workloads/${test}_tmp.f
            echo "ext4 ${nthread} " >> data/filebench_${test}.log
            sudo filebench -f ./filebench/workloads/${test}_tmp.f | grep "IO Summary" >> data/filebench_${test}.log
        done
    done
}

evaluate_f2fs() {
    echo "Evaluating f2fs filesystem..."
    
    # Test types
    
    tests=(fileserver webserver varmail webproxy)
    nthreads=(1 2 4 8 16 32 64)
    for test in "${tests[@]}"; do
        for nthread in "${nthreads[@]}"; do
            echo "Running f2fs test: $test $nthread"
            initialize_f2fs
            sed -e "s/^set \$nthreads=.*/set \$nthreads=$nthread/" -e "s|^set \$dir=.*|set \$dir=/mnt/eval_f2fs|" ./filebench/workloads/$test.f > ./filebench/workloads/${test}_tmp.f
            echo "f2fs ${nthread} " >> data/filebench_${test}.log
            sudo filebench -f ./filebench/workloads/${test}_tmp.f | grep "IO Summary" >> data/filebench_${test}.log
        done
    done
    echo "Completed f2fs test: $test"
    echo "------------------------"
}

# Main execution
main() {
    echo "Starting Figure 18 evaluation..."
    mkdir -p data
    mkdir -p temp
    sudo rm -f data/filebench_*.log
    make_filebench
    # evaluate_aeolia
    
    evaluate_ext4
    
    evaluate_f2fs
    
    echo "Figure 18 evaluation completed."
    sudo rm -rf temp
}

# Run main function
main "$@"
