/* $Id: pciids.h,v 1.6 2002/04/08 18:58:56 sandervl Exp $ */
/*
 * PCI ID definitions for the supported chipsets
 *
 * (C) 2000-2002 InnoTek Systemberatung GmbH
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

#ifndef __PCIIDS_H__
#define __PCIIDS_H__

#define PCI_VENDOR_ID_CMEDIA            0x13F6
#define PCI_DEVICE_ID_CMEDIA_CM8338A    0x0100
#define PCI_DEVICE_ID_CMEDIA_CM8338B    0x0101
#define PCI_DEVICE_ID_CMEDIA_CM8738     0x0111
#define PCI_DEVICE_ID_CMEDIA_CM8738B    0x0112

#define PCI_VENDOR_ID_INTEL             0x8086
#define PCI_DEVICE_ID_INTEL_82801       0x2415
#define PCI_DEVICE_ID_INTEL_82901       0x2425
#define PCI_DEVICE_ID_INTEL_82801BA     0x2445
#define PCI_DEVICE_ID_INTEL_440MX       0x7195
#define PCI_DEVICE_ID_INTEL_ICH3	    0x2485

#define PCI_VENDOR_ID_CIRRUS            0x1013
#define PCI_DEVICE_ID_CIRRUS_4281	    0x6005

#ifndef PCI_VENDOR_ID_ESS
#define PCI_VENDOR_ID_ESS               0x125D
#endif
#define PCI_DEVICE_ID_ESS_ALLEGRO_1	    0x1988
#define PCI_DEVICE_ID_ESS_ALLEGRO	    0x1989
#define PCI_DEVICE_ID_ESS_MAESTRO3	    0x1998
#define PCI_DEVICE_ID_ESS_MAESTRO3_1	0x1999
#define PCI_DEVICE_ID_ESS_MAESTRO3_HW	0x199a
#define PCI_DEVICE_ID_ESS_MAESTRO3_2	0x199b


#define PCIID_CREATIVELABS_SBLIVE  0x00021102
#define PCIID_VIA_686A             0x30581106
#define PCIID_VIA_8233             0x30591106
#define PCIID_ALS4000              0x40004005
#define PCIID_CMEDIA_CM8338A       ((PCI_DEVICE_ID_CMEDIA_CM8338A<<16) | PCI_VENDOR_ID_CMEDIA)
#define PCIID_CMEDIA_CM8338B       ((PCI_DEVICE_ID_CMEDIA_CM8338B<<16) | PCI_VENDOR_ID_CMEDIA)
#define PCIID_CMEDIA_CM8738        ((PCI_DEVICE_ID_CMEDIA_CM8738<<16)  | PCI_VENDOR_ID_CMEDIA)
#define PCIID_CMEDIA_CM8738B       ((PCI_DEVICE_ID_CMEDIA_CM8738B<<16) | PCI_VENDOR_ID_CMEDIA)
#define PCIID_INTEL_82801          ((PCI_DEVICE_ID_INTEL_82801<<16)    | PCI_VENDOR_ID_INTEL)
#define PCIID_INTEL_82901          ((PCI_DEVICE_ID_INTEL_82901<<16)    | PCI_VENDOR_ID_INTEL)
#define PCIID_INTEL_92801BA        ((PCI_DEVICE_ID_INTEL_82801BA<<16)  | PCI_VENDOR_ID_INTEL)
#define PCIID_INTEL_440MX          ((PCI_DEVICE_ID_INTEL_440MX<<16)    | PCI_VENDOR_ID_INTEL)
#define PCIID_INTEL_ICH3           ((PCI_DEVICE_ID_INTEL_ICH3<<16)     | PCI_VENDOR_ID_INTEL)
#define PCIID_CIRRUS_4281          ((PCI_DEVICE_ID_CIRRUS_4281<<16)    | PCI_VENDOR_ID_CIRRUS)
#define PCIID_ESS_ALLEGRO_1        ((PCI_DEVICE_ID_ESS_ALLEGRO_1<<16)  | PCI_VENDOR_ID_ESS)
#define PCIID_ESS_ALLEGRO          ((PCI_DEVICE_ID_ESS_ALLEGRO<<16)    | PCI_VENDOR_ID_ESS)
#define PCIID_ESS_MAESTRO3         ((PCI_DEVICE_ID_ESS_MAESTRO3<<16)   | PCI_VENDOR_ID_ESS)
#define PCIID_ESS_MAESTRO3_1       ((PCI_DEVICE_ID_ESS_MAESTRO3_1<<16) | PCI_VENDOR_ID_ESS)
#define PCIID_ESS_MAESTRO3_HW      ((PCI_DEVICE_ID_ESS_MAESTRO3_HW<<16)| PCI_VENDOR_ID_ESS)
#define PCIID_ESS_MAESTRO3_2       ((PCI_DEVICE_ID_ESS_MAESTRO3_2<<16) | PCI_VENDOR_ID_ESS)

#endif //__PCIIDS_H__
