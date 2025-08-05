set -e
shopt -s nullglob extglob

reset_hugepages() {
    if mountpoint -q /mnt/libstorage_huge; then
        echo "Unmounting /mnt/libstorage_huge..."
        sudo umount /mnt/libstorage_huge
    else
        echo "/mnt/libstorage_huge is not mounted."
    fi

    # echo "Setting HugePages to 2048..."
    sudo sh -c "echo 4096 > /sys/devices/system/node/node3/hugepages/hugepages-2048kB/nr_hugepages"
    sudo sh -c "echo 16384 > /proc/sys/vm/nr_hugepages"
    echo "Removing /mnt/libstorage_huge directory..."
    sudo rm -rf /mnt/libstorage_huge

    echo "Creating /mnt/libstorage_huge directory..."
    sudo mkdir -p /mnt/libstorage_huge

    echo "Mounting HugePages filesystem..."
    sudo mount -t hugetlbfs -o size=16G none /mnt/libstorage_huge

    if mountpoint -q /mnt/libstorage_huge; then
        echo "HugePages successfully reset and mounted at /mnt/libstorage_huge."
    else
        echo "Failed to mount HugePages."
    fi
}

build_libsched() {
    local libsched_path="scx/build/scheds/c/scx_eevdf"
    
    echo "Checking if libsched exists..."
    
    if [ ! -f "$libsched_path" ]; then
        echo "libsched not found at $libsched_path"
        echo "Building sched_ext schedulers..."
        
        if [ ! -d "scx" ]; then
            echo "Error: scx directory not found. Please run this script from the project root."
            return 1
        fi
        
        cd scx
        echo "Building..."
        
        meson="../tools/meson/meson.py"

        $meson setup build --prefix ~
        $meson compile -C build scx_eevdf
        
        if [ -f "build/scheds/c/scx_eevdf" ]; then
            echo "libsched built successfully!"
        else
            echo "Error: Failed to build libsched"
            return 1
        fi
        
        cd ..
    else
        echo "libsched already exists at $libsched_path"
    fi
}

reset_hugepages
build_libsched
