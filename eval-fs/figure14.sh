#!/bin/bash

# Initialize functions for different filesystems
make_aeolia_fio() {
    
    # Use Makefile with sufs support
    sudo make
    rm -f fio-3.32/Makefile

    cp fio-3.32/Makefile.sufs fio-3.32/Makefile
    
    # Build fxmark with sufs
    cd fio-3.32 && sudo ./build.sh && cd ..
}

make_ext4_fio() {
    cp fio-3.32/Makefile.nosufs fio-3.32/Makefile
    cd fio-3.32 && sudo ./build.sh && cd ..
}

initialize_aeolia() {
    echo "Initializing Aeolia filesystem..."
    sudo make clean && sudo make
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
    
    tests=(4K 2M)
    types=(read write)
    for test in "${tests[@]}"; do
        for type in "${types[@]}"; do
            echo "Running aeolia test: $test $type"
            initialize_aeolia
            sudo ./fio-3.32/fio --name=test --ioengine=sync --rw=$type --bs=$test --numjobs=1 --size=512M --runtime=5s --time_based --output=./temp/aeolia_${test}_${type}.json --directory=/sufs --filename=test --output-format=json
            sudo sed -i '/^fio:/d' ./temp/aeolia_${test}_${type}.json
            TP=$(jq '.jobs[0].'"${type}"'.bw_mean' ./temp/aeolia_${test}_${type}.json)
            echo "$type, $TP, aeolia" >> data/fio_single_${test}_data.csv
        done
    done
}

evaluate_ext4() {

    echo "Evaluating ext4 filesystem..."
    tests=(4K 2M)
    types=(read write)
    for test in "${tests[@]}"; do
        for type in "${types[@]}"; do
            echo "Running ext4 test: $test $type"
            initialize_ext4
            sudo ./fio-3.32/fio --name=test --ioengine=sync --rw=$type --bs=$test --thread=1 --numjobs=1 --size=512M --runtime=5s --time_based --output=./temp/ext4_${test}_${type}.json --directory=/mnt/eval_ext4 --output-format=json
            TP=$(jq '.jobs[0].'"${type}"'.bw_mean' ./temp/ext4_${test}_${type}.json)
            echo "$type, $TP, ext4" >> data/fio_single_${test}_data.csv
        done
    done
}

evaluate_f2fs() {
    echo "Evaluating f2fs filesystem..."
    
    # Test types
    tests=(4K 2M)
    types=(read write)
    for test in "${tests[@]}"; do
        for type in "${types[@]}"; do
            echo "Running f2fs test: $test $type"
            initialize_f2fs
            sudo ./fio-3.32/fio --name=test --ioengine=sync --rw=$type --bs=$test --thread=1 --numjobs=1 --size=512M --runtime=5s --time_based --output=./temp/f2fs_${test}_${type}.json --directory=/mnt/eval_f2fs --output-format=json
            TP=$(jq '.jobs[0].'"${type}"'.bw_mean' ./temp/f2fs_${test}_${type}.json)
            echo "$type, $TP, f2fs" >> data/fio_single_${test}_data.csv
        done
    done
    echo "Completed f2fs test: $test"
    echo "------------------------"
}

# Main execution
main() {
    echo "Starting Figure 14 evaluation..."
    mkdir -p data
    mkdir -p temp
    sudo rm data/fio_single_*_data.csv

    make_aeolia_fio
    evaluate_aeolia
    
    make_ext4_fio
    evaluate_ext4
    
    evaluate_f2fs
    
    echo "Figure 14 evaluation completed."
    sudo rm -rf temp
}

# Run main function
main "$@"
