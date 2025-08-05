#!/bin/bash

# Initialize functions for different filesystems
make_aeolia_fxmark() {
    
    # Use Makefile with sufs support
    sudo make
    rm -f fxmark-test/Makefile

    cp fxmark-test/Makefile.sufs fxmark-test/Makefile
    
    # Build fxmark with sufs
    cd fxmark-test && make clean && make && cd ..
}

make_ext4_fxmark() {
    cp fxmark-test/Makefile.nosufs fxmark-test/Makefile
    cd fxmark-test && make clean && make && cd ..
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
    
    # Test types
    tests=(MRPL MRPM MRPH MWCL MWCM MWUL MWUM MWRL MWRM)
    # Core counts
   cores=(1 2 4 8 16 24 32 48 56 64)
#    cores=(1 4 32 64)
    for test in "${tests[@]}"; do
        echo "Running Aeolia test: $test"
        
        # Initialize for Aeolia
        
        for core in "${cores[@]}"; do
            
            initialize_aeolia
            echo "  Testing with $core cores..."
            sudo ./fxmark-test/bin/fxmark \
                --type "$test" \
                --ncore "$core" \
                --nbg 0 \
                --duration 5 \
                --directio 0 \
                --root /sufs/ \
                --profbegin "echo start" \
                --profend "echo end" \
                --proflog "a.txt" \
                | tee >(grep "#" -A 1 | tail -n 1 >> data/fxmark_aeolia_${test}.log)
        done
        
        echo "Completed Aeolia test: $test"
        echo "------------------------"
    done
}

evaluate_ext4() {

    echo "Evaluating ext4 filesystem..."
    
    # Test types
    tests=(MRPL MRPM MRPH MWCL MWCM MWUL MWUM MWRL MWRM)
    # Core counts
    cores=(1 2 4 8 16 24 32 48 56 64)
    
    for test in "${tests[@]}"; do
        echo "Running ext4 test: $test"
        
        # Initialize for ext4
        
        for core in "${cores[@]}"; do
            initialize_ext4
            echo "  Testing with $core cores..."
            sudo ./fxmark-test/bin/fxmark \
                --type "$test" \
                --ncore "$core" \
                --nbg 0 \
                --duration 5 \
                --directio 0 \
                --root /mnt/eval_ext4/ \
                --profbegin "echo start" \
                --profend "echo end" \
                --proflog "a.txt" | grep "#" -A 1 | tail -n 1 >> data/fxmark_ext4_${test}.log
        done
        
        echo "Completed ext4 test: $test"
        echo "------------------------"
    done
}

evaluate_f2fs() {
    echo "Evaluating f2fs filesystem..."
    
    # Test types
    tests=(MRPL MRPM MRPH MWCL MWCM MWUL MWUM MWRL MWRM)
    # Core counts
    cores=(1 2 4 8 16 24 32 48 56 64)
    
    for test in "${tests[@]}"; do
        echo "Running f2fs test: $test"
        
        # Initialize for f2fs
        
        for core in "${cores[@]}"; do
            initialize_f2fs
            echo "  Testing with $core cores..."
            sudo ./fxmark-test/bin/fxmark \
                --type "$test" \
                --ncore "$core" \
                --nbg 0 \
                --duration 5 \
                --directio 0 \
                --root /mnt/eval_f2fs/ \
                --profbegin "echo start" \
                --profend "echo end" \
                --proflog "a.txt" | grep "#" -A 1 | tail -n 1 >> data/fxmark_f2fs_${test}.log
        done
        
        echo "Completed f2fs test: $test"
        echo "------------------------"
    done
}

# Main execution
main() {
    echo "Starting Figure 16 evaluation..."
    mkdir -p data
    make_aeolia_fxmark
    evaluate_aeolia
    
    make_ext4_fxmark
    evaluate_ext4
    
    evaluate_f2fs
    
    echo "Figure 16 evaluation completed."
}

# Run main function
main "$@"
