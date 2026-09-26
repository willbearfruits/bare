#pragma once
/* Internal disks: SATA through AHCI, NVMe, and NVMe behind Intel's VMD. Each driver adds its disks here and the platform
   lists them after the USB sticks. They start only on boots without the BIOS's disk services: those reach internal
   disks themselves, and two drivers must never share a controller. Sectors are 512 bytes to the rest of the system
   (a 4K NVMe namespace is read and written around that). */
#include <stdint.h>
#include <stdbool.h>

#define BLKDEVS 8
struct blkdev {
    char name[48];                              /* "SATA 1: Samsung SSD 860 EVO" */
    uint64_t sectors;
    bool (*io)(struct blkdev *d, uint64_t lba, uint32_t n, void *buf, bool write);
    void *ctx;
};
struct blkdev *blkdev_add(void);                /* 0 when the list is full */
int  blkdev_count(void);
struct blkdev *blkdev_get(int i);
bool blkdev_io(int i, uint64_t lba, uint32_t n, void *buf, bool write);

void internal_disks_init(void);                 /* AHCI, NVMe and VMD controllers, and the disks on them */
int  ahci_init(void);
int  nvme_init(void);
int  vmd_init(void);
