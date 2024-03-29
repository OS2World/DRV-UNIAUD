/* $Id: types.h,v 1.1 2001/08/16 07:46:01 sandervl Exp $ */

#ifndef _I386_TYPES_H
#define _I386_TYPES_H

typedef unsigned short umode_t;

/*
 * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space
 */

typedef signed char __s8;
typedef unsigned char __u8;

typedef signed short __s16;
typedef unsigned short __u16;

typedef signed int __s32;
typedef unsigned int __u32;

//#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __int64 __s64;
typedef unsigned __int64 __u64;
//#endif

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed __int64 s64;
typedef unsigned __int64 u64;


#endif
