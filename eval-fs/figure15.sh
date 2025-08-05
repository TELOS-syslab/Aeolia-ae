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
    
    tests=(4K)
    types=(read)
    cores=(1 2 4 8 16 32 64)
    # cores=(1 8 32 64)
    for test in "${tests[@]}"; do
        for type in "${types[@]}"; do
            for core in "${cores[@]}"; do
                echo "Running aeolia test: $test $type $core"
                initialize_aeolia
                sudo ./fio-3.32/fio --name=test --ioengine=sync --rw=$type --bs=$test --thread=1 --numjobs=$core --size=512M --runtime=5s --time_based --output=./temp/aeolia_${test}_${type}_${core}.json --directory=/sufs/ --output-format=json --cpus_allowed=1-64 --cpus_allowed_policy=split
                sudo sed -i '/^fio:/d' ./temp/aeolia_${test}_${type}_${core}.json
                TP=$(jq '.jobs[0].'"${type}"'.bw_mean' ./temp/aeolia_${test}_${type}_${core}.json)
                echo "aeolia, $core, $TP" >> data/fio_multi_${test}_${type}.csv
            done
        done
    done
}

evaluate_ext4() {

    echo "Evaluating ext4 filesystem..."
    tests=(4K)
    types=(read)
    cores=(1 2 4 8 16 32 64)
    for test in "${tests[@]}"; do
        for type in "${types[@]}"; do
            for core in "${cores[@]}"; do
                echo "Running ext4 test: $test $type $core"
                initialize_ext4
                sudo ./fio-3.32/fio --name=test --ioengine=sync --rw=$type --bs=$test --thread=1 --numjobs=$core --size=512M --runtime=5s --time_based --output=./temp/ext4_${test}_${type}_${core}.json --directory=/mnt/eval_ext4 --output-format=json --cpus_allowed=1-64 --cpus_allowed_policy=split
                TP=$(jq '.jobs[0].'"${type}"'.bw_mean' ./temp/ext4_${test}_${type}_${core}.json)
                echo "ext4, $core, $TP" >> data/fio_multi_${test}_${type}.csv
            done
        done
    done
}

evaluate_f2fs() {
    echo "Evaluating f2fs filesystem..."
    
    # Test types
    tests=(4K)
    types=(read)
    cores=(1 2 4 8 16 32 64)
    # cores=(1 8 32 64)
    for test in "${tests[@]}"; do
        for type in "${types[@]}"; do
            for core in "${cores[@]}"; do
                echo "Running f2fs test: $test $type $core"
                initialize_f2fs
                sudo ./fio-3.32/fio --name=test --ioengine=sync --rw=$type --bs=$test --thread=1 --numjobs=$core --size=512M --runtime=5s --time_based --output=./temp/f2fs_${test}_${type}_${core}.json --directory=/mnt/eval_f2fs --output-format=json --cpus_allowed=1-64 --cpus_allowed_policy=split
                TP=$(jq '.jobs[0].'"${type}"'.bw_mean' ./temp/f2fs_${test}_${type}_${core}.json)
                echo "f2fs, $core, $TP" >> data/fio_multi_${test}_${type}.csv
            done
        done
    done
    echo "Completed f2fs test: $test"
    echo "------------------------"
}


# Main execution
main() {
    echo "Starting Figure 15 evaluation..."
    mkdir -p data
    mkdir -p temp
    sudo rm -f data/fio_multi_*.csv
    
    make_aeolia_fio
    evaluate_aeolia
    
    make_ext4_fio
    evaluate_ext4
    
    evaluate_f2fs
    
    echo "Figure 15 evaluation completed."
    sudo rm -f temp_*.csv
    # rm -rf temp
}

# Run main function
main "$@"
