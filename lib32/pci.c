/* $Id: pci.c,v 1.27 2002/07/25 10:13:49 sandervl Exp $ */
/*
 * OS/2 implementation of Linux PCI functions (using direct port I/O)
 *
 * (C) 2000-2002 InnoTek Systemberatung GmbH
 * (C) 2000-2001 Sander van Leeuwen (sandervl@xs4all.nl)
 *
 * Parts based on Linux kernel sources
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#include "linux.h"
#include <linux/init.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <sound/config.h>
#include <sound/asound.h>

#define LINUX
#include <ossidc.h>
#include <stacktoflat.h>
#include <dbgos2.h>
#include <osspci.h>

struct pci_dev pci_devices[MAX_PCI_DEVICES] = {0};
struct pci_bus pci_busses[MAX_PCI_BUSSES] = {0};

HRESMGR hResMgr = 0;

//******************************************************************************
//******************************************************************************
OSSRET OSS32_APMResume()
{
    int i;
    struct pci_driver *driver;

    dprintf(("OSS32_APMResume"));
    for(i=0;i<MAX_PCI_DEVICES;i++) 
    {
        if(pci_devices[i].devfn) 
        {
            driver = pci_devices[i].pcidriver;
            if(driver->resume) {
                driver->resume(&pci_devices[i]);
            }
        }
    }
    return OSSERR_SUCCESS;
}
//******************************************************************************
//******************************************************************************
OSSRET OSS32_APMSuspend()
{
    int i;
    struct pci_driver *driver;
    
    dprintf(("OSS32_APMSuspend"));
    for(i=0;i<MAX_PCI_DEVICES;i++) 
    {
        if(pci_devices[i].devfn) 
        {
            driver = pci_devices[i].pcidriver;
            if(driver->suspend) {
                driver->suspend(&pci_devices[i], SNDRV_CTL_POWER_D3cold);
            }
        }
    }
    return OSSERR_SUCCESS;
}
//******************************************************************************
//******************************************************************************
int pcidev_prepare(struct pci_dev *dev)
{
    dprintf(("pcidev_prepare %x not implemented", dev));
    return 1; //todo: correct return value??
}
//******************************************************************************
//******************************************************************************
int pcidev_activate(struct pci_dev *dev)
{
    dprintf(("pcidev_activate %x not implemented", dev));
    return 1; //todo: correct return value??
}
//******************************************************************************
//******************************************************************************
int pcidev_deactivate(struct pci_dev *dev)
{
    dprintf(("pcidev_deactivate %x not implemented", dev));
    return 1; //todo: correct return value??
}
//******************************************************************************
//TODO: Doesn't completely fill in the pci_dev structure
//******************************************************************************
int FindPCIDevice(unsigned int vendor, unsigned int device, struct pci_dev near *pcidev)
{
    IDC_RESOURCE idcres;
    int i, residx = 0;

    pcidev->prepare    = pcidev_prepare;
    pcidev->activate   = pcidev_activate;
    pcidev->deactivate = pcidev_deactivate;
    pcidev->active     = 1;
    pcidev->ro         = 0;
    pcidev->sibling    = NULL;
    pcidev->next       = NULL;
    pcidev->vendor     = vendor;
    pcidev->device     = device;
    pcidev->dma_mask   = 0xFFFFFFFF;
	
    hResMgr = RMFindPCIDevice(vendor, device, &idcres);
    if(hResMgr == 0) {
        return FALSE;
    }
    pcidev->devfn = idcres.devfn;

    for(i=0;i<MAX_RES_IO;i++) {
        if(idcres.io[i] != 0xffff) {
            pcidev->resource[residx].name  = 0;
            pcidev->resource[residx].child = 0;
            pcidev->resource[residx].sibling = 0;
            pcidev->resource[residx].parent = 0;
            pcidev->resource[residx].start = idcres.io[i];
            pcidev->resource[residx].end   = idcres.io[i] + idcres.iolength[i]; //inclusive??
            pcidev->resource[residx].flags = IORESOURCE_IO | PCI_BASE_ADDRESS_SPACE_IO;

            residx++;
        }
    }
    for(i=0;i<MAX_RES_MEM;i++) {
        if(idcres.mem[i] != 0xffffffff) {
            pcidev->resource[residx].name  = 0;
            pcidev->resource[residx].child = 0;
            pcidev->resource[residx].sibling = 0;
            pcidev->resource[residx].parent = 0;
            pcidev->resource[residx].start = idcres.mem[i];
            pcidev->resource[residx].end   = idcres.mem[i] + idcres.memlength[i]; //inclusive??
            pcidev->resource[residx].flags = IORESOURCE_MEM | IORESOURCE_MEM_WRITEABLE;

            residx++;
        }
    }
    for(i=0;i<MAX_RES_DMA;i++) {
        if(idcres.dma[i] != 0xffff) {
            pcidev->dma_resource[i].name  = 0;
            pcidev->dma_resource[i].child = 0;
            pcidev->dma_resource[i].sibling = 0;
            pcidev->dma_resource[i].parent = 0;
            pcidev->dma_resource[i].start = idcres.dma[i];
            pcidev->dma_resource[i].end   = idcres.dma[i];
            //todo: 8/16 bits
            pcidev->dma_resource[i].flags = IORESOURCE_DMA;
        }
    }
    for(i=0;i<MAX_RES_IRQ;i++) {
        if(idcres.irq[i] != 0xffff) {
            pcidev->irq_resource[i].name  = 0;
            pcidev->irq_resource[i].child = 0;
            pcidev->irq_resource[i].sibling = 0;
            pcidev->irq_resource[i].parent = 0;
            pcidev->irq_resource[i].start = idcres.irq[i];
            pcidev->irq_resource[i].end   = idcres.irq[i];
            //todo: irq flags
            pcidev->irq_resource[9].flags = IORESOURCE_IRQ;
        }
    }
    pcidev->irq = pcidev->irq_resource[0].start;

    if(idcres.busnr > MAX_PCI_BUSSES) {
        DebugInt3();
        return FALSE;
    }
    pcidev->bus = &pci_busses[idcres.busnr];
    pcidev->bus->number = idcres.busnr;

    pci_read_config_word(pcidev, PCI_SUBSYSTEM_VENDOR_ID, &pcidev->subsystem_vendor);
    pci_read_config_word(pcidev, PCI_SUBSYSTEM_ID, &pcidev->subsystem_device);

    return TRUE;
}
//******************************************************************************
//******************************************************************************
struct pci_dev *pci_find_device (unsigned int vendor, unsigned int device, struct pci_dev *from)
{
    int i;
    HRESMGR hResMgrTmp = 0;

    if(from) {
        //requesting 2nd device of the same type; don't support this for now
        DebugInt3();
        return 0;
    }
    //not very pretty
    if(hResMgr) {
        hResMgrTmp = hResMgr;
        hResMgr    = 0;
    }
    for(i=0;i<MAX_PCI_DEVICES;i++) 
    {
        if(pci_devices[i].devfn == 0) 
        {
            if(FindPCIDevice(vendor, device, (struct pci_dev near *)&pci_devices[i]) == TRUE) {
                if(hResMgrTmp) {
                    RMDestroy(hResMgr);
                    hResMgr = hResMgrTmp;
                }
                return &pci_devices[i];
            }
            break;
        }
    }
    if(hResMgrTmp) {
        RMDestroy(hResMgr);
        hResMgr = hResMgrTmp;
    }
    return 0;
}
//******************************************************************************
//******************************************************************************
struct resource * __request_region(struct resource *a, unsigned long start, unsigned long n, const char *name)
{
	struct resource *resource;

    if(a->flags & IORESOURCE_MEM) {
        if(RMRequestMem(hResMgr, start, n) == FALSE) {
            dprintf(("RMRequestIO failed for io %x, length %x", start, n));
            return NULL;
        }
    }
    else
    if(a->flags & IORESOURCE_IO) {
        if(RMRequestIO(hResMgr, start, n) == FALSE) {
            dprintf(("RMRequestIO failed for io %x, length %x", start, n));
            return NULL;
        }
    }

	resource = kmalloc(sizeof(struct resource), GFP_KERNEL);
	if (resource == NULL)
		return NULL;
	resource->name  = name;
	resource->start = start;
	resource->end   = start + n - 1;
	resource->flags = a->flags;
	return resource;
}
//******************************************************************************
//******************************************************************************
void __release_region(struct resource *resource, unsigned long b, unsigned long c)
{
    if(resource) {
        kfree(resource);
    }
}
//******************************************************************************
#define CONFIG_CMD(dev, where)   (0x80000000 | (dev->bus->number << 16) | (dev->devfn << 8) | (where & ~3))
//******************************************************************************
int pci_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);
    *value = inb(0xCFC + (where&3));
    return PCIBIOS_SUCCESSFUL;
}
//******************************************************************************
//******************************************************************************
int pci_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);    
    *value = inw(0xCFC + (where&2));
    return PCIBIOS_SUCCESSFUL;    
}
//******************************************************************************
//******************************************************************************
int pci_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);
    *value = inl(0xCFC);
    return PCIBIOS_SUCCESSFUL;    
}
//******************************************************************************
//******************************************************************************
int pci_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);    
    outb(value, 0xCFC + (where&3));
    return PCIBIOS_SUCCESSFUL;
}
//******************************************************************************
//******************************************************************************
int pci_write_config_word(struct pci_dev *dev, int where, u16 value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);
    outw(value, 0xCFC + (where&2));
    return PCIBIOS_SUCCESSFUL;
}
//******************************************************************************
//******************************************************************************
int pci_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
    outl(CONFIG_CMD(dev,where), 0xCF8);
    outl(value, 0xCFC);
    return PCIBIOS_SUCCESSFUL;
}
//******************************************************************************
//******************************************************************************
int pcibios_present(void)
{
    dprintf(("pcibios_present -> pretend BIOS present"));
    return 1;
}
//******************************************************************************
//******************************************************************************
struct pci_dev *pci_find_slot (unsigned int bus, unsigned int devfn)
{
    dprintf(("pci_find_slot %d %x not implemented!!", bus, devfn));
    DebugInt3();
    return NULL;
}
//******************************************************************************
//******************************************************************************
int pci_dma_supported(struct pci_dev *dev, unsigned long mask)
{
    dprintf(("pci_dma_supported: return TRUE"));
    return 1;
}
//******************************************************************************
//******************************************************************************
int pci_enable_device(struct pci_dev *dev)
{
    dprintf(("pci_enable_device %x: not implemented", dev));
    return 0;
}
//******************************************************************************
//******************************************************************************
int pci_register_driver(struct pci_driver *driver)
{
    struct pci_dev *pcidev;
    int i = 0;

    while(driver->id_table[i].vendor) 
    {
        pcidev = pci_find_device(driver->id_table[i].vendor, driver->id_table[i].device, NULL);
        if(pcidev && driver->probe) {
            if(driver->probe(pcidev, &driver->id_table[i]) == 0) {
                //remove resource manager object for this device and
                //register resources with RM
                RMFinialize(hResMgr);
                hResMgr = 0;

                //save driver pointer for suspend/resume calls
                pcidev->pcidriver = (void *)driver;
                return 1;
            }
            else pcidev->devfn = NULL;
            RMDestroy(hResMgr);
            hResMgr = 0;
        }
        i++;
    }
    return 0;
}
//******************************************************************************
//******************************************************************************
int pci_module_init(struct pci_driver *drv)
{
    int res = pci_register_driver(drv);
    if (res < 0)
        return res;
    if (res == 0)
        return -ENODEV;
    return 0;
}
//******************************************************************************
//******************************************************************************
int pci_unregister_driver(struct pci_driver *driver)
{
    struct pci_dev *pcidev;
    int i = 0, j;

    while(driver->id_table[i].vendor) 
    {
        for(j=0;j<MAX_PCI_DEVICES;j++) 
        {
            if(pci_devices[j].vendor == driver->id_table[i].vendor &&
               pci_devices[j].device == driver->id_table[i].device)
            {
                if(driver->remove) {
                    driver->remove(&pci_devices[j]);
                }
            }
        }
        i++;
    }
    return 0;
}
//******************************************************************************
//******************************************************************************
void pci_set_master(struct pci_dev *dev)
{
	u16 cmd;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (! (cmd & PCI_COMMAND_MASTER)) {
        dprintf(("pci_set_master %x", dev));
		cmd |= PCI_COMMAND_MASTER;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
    return;
}
//******************************************************************************
//******************************************************************************
int __compat_get_order(unsigned long size)
{
    int order;

    size = (size-1) >> (PAGE_SHIFT-1);
    order = -1;
    do {
        size >>= 1;
        order++;
    } while (size);
    return order;
}
//******************************************************************************
//******************************************************************************
void *pci_alloc_consistent(struct pci_dev *hwdev,
                           long size, dma_addr_t *dma_handle) 
{
    void *ret = NULL;
    int gfp = GFP_ATOMIC;

    dprintf(("pci_alloc_consistent %d mask %x", size, (hwdev) ? hwdev->dma_mask : 0));
    if (hwdev == NULL || hwdev->dma_mask != 0xffffffff) {
        //try not to exhaust low memory (< 16mb) so allocate from the high region first
        //if that doesn't satisfy the dma mask requirement, then get it from the low
        //regino anyway
        if(hwdev->dma_mask > 0x00ffffff) {
            ret = (void *)__get_free_pages(gfp|GFP_DMAHIGHMEM, __compat_get_order(size));
            *dma_handle = virt_to_bus(ret);
            if(*dma_handle > hwdev->dma_mask) {
                free_pages((unsigned long)ret, __compat_get_order(size));
                //be sure and allocate below 16 mb
                gfp |= GFP_DMA;
                ret = NULL;
            }
        }
        else { //must always allocate below 16 mb
            gfp |= GFP_DMA;
        }
    }
    if(ret == NULL) {
        ret = (void *)__get_free_pages(gfp, __compat_get_order(size));
    }

    if (ret != NULL) {
        memset(ret, 0, size);
        *dma_handle = virt_to_bus(ret);
    }
    return ret;
}
//******************************************************************************
//******************************************************************************
void pci_free_consistent(struct pci_dev *hwdev, long size,
                         void *vaddr, dma_addr_t dma_handle)
{
    free_pages((unsigned long)vaddr, __compat_get_order(size));
}
//******************************************************************************
//******************************************************************************
void pci_set_driver_data (struct pci_dev *dev, void *driver_data)
{
    if (dev)
        dev->driver_data = driver_data;
}
//******************************************************************************
//******************************************************************************
void *pci_get_driver_data (struct pci_dev *dev)
{
    if (dev)
        return dev->driver_data;
    return 0;
}
//******************************************************************************
//******************************************************************************
unsigned long pci_get_dma_mask (struct pci_dev *dev)
{
    if (dev)
        return dev->dma_mask;
    return 0;
}
//******************************************************************************
//******************************************************************************
void pci_set_dma_mask (struct pci_dev *dev, unsigned long mask)
{
    if (dev)
        dev->dma_mask = mask;
}
//******************************************************************************
//******************************************************************************
int release_resource(struct resource *newres)
{
    return 0;
}
//******************************************************************************
//******************************************************************************
int pci_set_power_state(struct pci_dev *dev, int state)
{
    dprintf(("pci_set_power_state %x %d NOT implemented", dev, state)); 
    return 0;
}
//******************************************************************************
//******************************************************************************
int pci_get_flags (struct pci_dev *dev, int n_base)
{
	unsigned long foo = dev->resource[n_base].flags & PCI_BASE_ADDRESS_SPACE;
	int flags = 0;
	
	if (foo == 0)
		flags |= IORESOURCE_MEM;
	if (foo == 1)
		flags |= IORESOURCE_IO;
	
	return flags;
}
