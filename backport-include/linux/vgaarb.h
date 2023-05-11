#ifndef _BACKPORT_LINUX_VGAARB_H
#define _BACKPORT_LINUX_VGAARB_H
#include <linux/version.h>
#include_next <linux/vgaarb.h>
int vga_remove_vgacon(struct pci_dev *pdev);
#endif
