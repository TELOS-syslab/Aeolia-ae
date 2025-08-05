
# LibStorage Driver

## Provided APIs

### Init & Destroy APIs


### Request APIs

# LibStorage Kernel Module

This kernel module provides ability to the userspace driver. Mainly inclueds **Create queue pairs for userspace driver**, **Mapping qp memory like doorbells and queues into userspace**, **Manage IRQs for qps**, **Provide schedule interfaces for the co-design**

It will expose a device called libstorage, including 4 APIs for now, they are :
- OPEN/CLOSE Device
- CREATE/DELETE Qpair
Which are defined at include/libstorage_ioctl.h