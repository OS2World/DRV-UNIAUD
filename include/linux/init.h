/* $Id: init.h,v 1.2 2001/08/16 20:01:04 sandervl Exp $ */

#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

/* These macros are used to mark some functions or 
 * initialized data (doesn't apply to uninitialized data)
 * as `initialization' functions. The kernel can take this
 * as hint that the function is used only during the initialization
 * phase and free up used memory resources after
 *
 * Usage:
 * For functions:
 * 
 * You should add __init immediately before the function name, like:
 *
 * static void __init initme(int x, int y)
 * {
 *    extern int z; z = x * y;
 * }
 *
 * If the function has a prototype somewhere, you can also add
 * __init between closing brace of the prototype and semicolon:
 *
 * extern int initialize_foobar_device(int, int, int) __init;
 *
 * For initialized data:
 * You should insert __initdata between the variable name and equal
 * sign followed by value, e.g.:
 *
 * static int init_variable __initdata = 0;
 * static char linux_logo[] __initdata = { 0x32, 0x36, ... };
 *
 * For initialized data not at file scope, i.e. within a function,
 * you should use __initlocaldata instead, due to a bug in GCC 2.7.
 */

#ifndef MODULE

#ifndef __ASSEMBLY__

/*
 * Used for kernel command line parameter setup
 */
struct kernel_param {
	const char *str;
	int (*setup_func)(char *);
};

extern struct kernel_param __setup_start, __setup_end;

#define __setup(str, fn)								\
	static char __setup_str_##fn[] __initdata = str;				\
	static struct kernel_param __setup_##fn __initsetup = { __setup_str_##fn, fn }

#endif /* __ASSEMBLY__ */

/*
 * Used for initialization calls..
 */
typedef int  (*initcall_t)(void);
typedef void (*exitcall_t)(void);

#define __initcall(fn)								\
	initcall_t __initcall_##fn  = fn

#define __exitcall(fn)								\
	exitcall_t __exitcall_##fn = fn


/*
 * Mark functions and data as being only used at initialization
 * or exit time.
 */
#define __init
#define __exit
#define __initdata
#define __exitdata
#define __initsetup
//#define __init_call
#define __devinitdata
#define __devinit
#define __devexit

/* For assembly routines */
#define __INIT
#define __FINIT
#define __INITDATA

#define module_init(x)	__initcall(x);
#define module_exit(x)	__exitcall(x);

#define extern_module_init(x)	extern initcall_t __initcall_##x;
#define extern_module_exit(x)   extern exitcall_t __exitcall_##x;

#else

#define __init
#define __exit
#define __initdata
#define __exitdata
//#define __initcall
/* For assembly routines */
#define __INIT
#define __FINIT
#define __INITDATA
#define __devinitdata
#define __devinit
#define __devexit

/*
 * Used for initialization calls..
 */
typedef int  (*initcall_t)(void);
typedef void (*exitcall_t)(void);

#define __initcall(fn)								\
	initcall_t __initcall_##fn  = fn

#define __exitcall(fn)								\
	exitcall_t __exitcall_##fn = fn


#define module_init(x)	__initcall(x);
#define module_exit(x)	__exitcall(x);

#define extern_module_init(x)	extern initcall_t __initcall_##x;
#define extern_module_exit(x)   extern exitcall_t __exitcall_##x;

#define __setup(str,func) /* nothing */

#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 8)
#define __initlocaldata  __initdata
#else
#define __initlocaldata
#endif

#endif /* _LINUX_INIT_H */
