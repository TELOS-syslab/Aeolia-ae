set -e
shopt -s nullglob extglob

reset_hugepages() {
    if mountpoint -q /mnt/libstorage_huge; then
        echo "Unmounting /mnt/libstorage_huge..."
        umount /mnt/libstorage_huge
    else
        echo "/mnt/libstorage_huge is not mounted."
    fi

    # echo "Setting HugePages to 2048..."
    # echo 65536 > /proc/sys/vm/nr_hugepages
    echo 65536 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages 
    echo 65536 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages 
    echo 65536 > /sys/devices/system/node/node2/hugepages/hugepages-2048kB/nr_hugepages 
    # echo 16384 > /proc/sys/vm/nr_hugepages
    echo "Removing /mnt/libstorage_huge directory..."
    rm -rf /mnt/libstorage_huge

    echo "Creating /mnt/libstorage_huge directory..."
    mkdir -p /mnt/libstorage_huge

    echo "Mounting HugePages filesystem..."
    mount -t hugetlbfs -o size=384G none /mnt/libstorage_huge

    if mountpoint -q /mnt/libstorage_huge; then
        echo "HugePages successfully reset and mounted at /mnt/libstorage_huge."
    else
        echo "Failed to mount HugePages."
    fi
}
reset_hugepages