/*
 *  Configuration header file for compilation of the ALSA driver
 */

#ifndef __ALSA_CONFIG_H__
#define __ALSA_CONFIG_H__

#define inline __inline
#define __attribute__

#include <linux/version.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/signal.h>
#include <linux/kdev_t.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/dcache.h>
#include <linux/vmalloc.h>
#include <linux/tqueue.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/stat.h>
#include <linux/major.h>
#include <linux/byteorder/little_endian.h>
#include <asm/ioctl.h>
#include <asm/hardirq.h>
#include <asm/processor.h>
#include <asm/siginfo.h>
#include <dbgos2.h>

extern int this_module[64];
#define THIS_MODULE (void *)&this_module[0]
#define MODULE_GENERIC_TABLE(gtype,name)        
#define MODULE_DEVICE_TABLE(type,name)         
#define CONFIG_PROC_FS
#define CONFIG_PM
#define PCI_NEW_SUSPEND
#define CONFIG_PCI
#define CONFIG_SND_SEQUENCER
////#define CONFIG_SND_OSSEMUL
#define SNDRV_LITTLE_ENDIAN
#define EXPORT_SYMBOL(a)
#define CONFIG_SOUND
#define CONFIG_SND_VERSION	"0.0.1"
#define ATTRIBUTE_UNUSED

#undef interrupt

/*
 * Power management requests
 */
enum
{
	PM_SUSPEND, /* enter D1-D3 */
	PM_RESUME,  /* enter D0 */

	/* enable wake-on */
	PM_SET_WAKEUP,

	/* bus resource management */
	PM_GET_RESOURCES,
	PM_SET_RESOURCES,

	/* base station management */
	PM_EJECT,
	PM_LOCK,
};

typedef int pm_request_t;

/*
 * Device types
 */
enum
{
	PM_UNKNOWN_DEV = 0, /* generic */
	PM_SYS_DEV,	    /* system device (fan, KB controller, ...) */
	PM_PCI_DEV,	    /* PCI device */
	PM_USB_DEV,	    /* USB device */
	PM_SCSI_DEV,	    /* SCSI device */
	PM_ISA_DEV,	    /* ISA device */
};

typedef int pm_dev_t;

/*
 * System device hardware ID (PnP) values
 */
enum
{
	PM_SYS_UNKNOWN = 0x00000000, /* generic */
	PM_SYS_KBC =	 0x41d00303, /* keyboard controller */
	PM_SYS_COM =	 0x41d00500, /* serial port */
	PM_SYS_IRDA =	 0x41d00510, /* IRDA controller */
	PM_SYS_FDC =	 0x41d00700, /* floppy controller */
	PM_SYS_VGA =	 0x41d00900, /* VGA controller */
	PM_SYS_PCMCIA =	 0x41d00e00, /* PCMCIA controller */
};

/*
 * Request handler callback
 */
struct pm_dev;

typedef int (*pm_callback)(struct pm_dev *dev, pm_request_t rqst, void *data);

/*
 * Dynamic device information
 */
struct pm_dev
{
	pm_dev_t	 type;
	unsigned long	 id;
	pm_callback	 callback;
	void		*data;

	unsigned long	 flags;
	int		 state;
	int		 prev_state;

	struct list_head entry;
};

int pm_init(void);
void pm_done(void);

#define CONFIG_PM

#define PM_IS_ACTIVE() 1

/*
 * Register a device with power management
 */
struct pm_dev *pm_register(pm_dev_t type,
			   unsigned long id,
			   pm_callback callback);

/*
 * Unregister a device with power management
 */
void pm_unregister(struct pm_dev *dev);

/*
 * Send a request to a single device
 */
int pm_send(struct pm_dev *dev, pm_request_t rqst, void *data);

#define fops_get(x) (x)
#define fops_put(x) do { ; } while (0)

#define __builtin_return_address(a)	0
#define SetPageReserved(a)		a
#define ClearPageReserved(a)		a
#define set_current_state(a)
#define try_inc_mod_count(x) 	        ++(*(unsigned long *)x)

#define rwlock_init(x) *(x) = RW_LOCK_UNLOCKED;

#define suser()	1

#define snd_kill_fasync(fp, sig, band) kill_fasync(*(fp), sig, band)


//what's this??
#define capable(a)	1
#define CAP_SYS_ADMIN   0


#endif //__ALSA_CONFIG_H__