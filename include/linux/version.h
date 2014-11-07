/* $Id: version.h,v 1.1 2001/08/16 07:47:13 sandervl Exp $ */

#ifndef _LINUX_VERSION_H
#define _LINUX_VERSION_H

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

#define LINUX_VERSION_CODE 0x020300

#define UTS_RELEASE "OS/2"

#endif /* _LINUX_VERSION_H */
