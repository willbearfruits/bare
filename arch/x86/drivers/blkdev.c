#include "blkdev.h"
#include "libc.h"
#include "log.h"

static struct blkdev devs[BLKDEVS];
static int ndevs;

struct blkdev *blkdev_add(void) {
    if (ndevs == BLKDEVS) return 0;
    memset(&devs[ndevs], 0, sizeof devs[ndevs]);
    return &devs[ndevs++];
}
int blkdev_count(void) { return ndevs; }
struct blkdev *blkdev_get(int i) { return i >= 0 && i < ndevs ? &devs[i] : 0; }
bool blkdev_io(int i, uint64_t lba, uint32_t n, void *buf, bool write) {
    struct blkdev *d = blkdev_get(i);
    if (!d || !n || lba + n > d->sectors) return false;
    return d->io(d, lba, n, buf, write);
}

void internal_disks_init(void) {
    static bool done;
    if (done) return;
    done = true;
    int n = ahci_init() + nvme_init() + vmd_init();
    for (int i = 0; i < ndevs; i++) logf("disk: %s, %lu MiB", devs[i].name, devs[i].sectors >> 11);
    if (!n) logf("disk: no internal disk (SATA or NVMe) found");
}
