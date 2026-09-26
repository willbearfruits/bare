#pragma once
#include <stdint.h>
#include "pci.h"
/* an NVMe function: on the ordinary PCI buses (pd), or behind VMD, whose configuration space is memory (cfg) and whose
   devices' addresses may be offset from what their BARs say */
struct nvme_fn { struct pci_dev pd; volatile uint8_t *cfg; uint64_t bar_offset; char name[16]; };
int      nvme_add(const struct nvme_fn *fn);          /* 1 if its namespace became a disk */
uint32_t nvme_cfg_rd(const struct nvme_fn *fn, uint32_t off);
void     nvme_cfg_wr(const struct nvme_fn *fn, uint32_t off, uint32_t v);
