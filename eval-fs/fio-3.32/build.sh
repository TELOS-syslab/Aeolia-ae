./configure --disable-rdma --disable-tcmalloc --disable-pmem --disable-http
make -j$(nproc)
