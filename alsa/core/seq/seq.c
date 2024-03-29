/*
 *  ALSA sequencer main module
 *  Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@home.nl>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#include <sound/initval.h>

#include <sound/seq_kernel.h>
#include "seq_clientmgr.h"
#include "seq_memory.h"
#include "seq_queue.h"
#include "seq_lock.h"
#include "seq_timer.h"
#include "seq_system.h"
#include "seq_info.h"
#include <sound/seq_device.h>

#ifdef TARGET_OS2
int snd_seq_client_load[64] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
#else
int snd_seq_client_load[64] = {[0 ... 63] = -1};
#endif
int snd_seq_default_timer_class = SNDRV_TIMER_CLASS_GLOBAL;
int snd_seq_default_timer_sclass = SNDRV_TIMER_SCLASS_NONE;
int snd_seq_default_timer_card = -1;
int snd_seq_default_timer_device = SNDRV_TIMER_GLOBAL_SYSTEM;
int snd_seq_default_timer_subdevice = 0;
int snd_seq_default_timer_resolution = 250;	/* Hz */

MODULE_AUTHOR("Frank van de Pol <fvdpol@home.nl>, Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture sequencer.");
MODULE_CLASSES("{sound}");
MODULE_SUPPORTED_DEVICE("sound");

MODULE_PARM(snd_seq_client_load, "i");
MODULE_PARM_DESC(snd_seq_client_load, "The numbers of global (system) clients to load through kmod.");
MODULE_PARM(snd_seq_default_timer_class, "i");
MODULE_PARM_DESC(snd_seq_default_timer_class, "The default timer class.");
MODULE_PARM(snd_seq_default_timer_sclass, "i");
MODULE_PARM_DESC(snd_seq_default_timer_sclass, "The default timer slave class.");
MODULE_PARM(snd_seq_default_timer_card, "i");
MODULE_PARM_DESC(snd_seq_default_timer_card, "The default timer card number.");
MODULE_PARM(snd_seq_default_timer_device, "i");
MODULE_PARM_DESC(snd_seq_default_timer_device, "The default timer device number.");
MODULE_PARM(snd_seq_default_timer_subdevice, "i");
MODULE_PARM_DESC(snd_seq_default_timer_subdevice, "The default timer subdevice number.");
MODULE_PARM(snd_seq_default_timer_resolution, "i");
MODULE_PARM_DESC(snd_seq_default_timer_resolution, "The default timer resolution in Hz.");

/*
 *  INIT PART
 */


static int __init alsa_seq_init(void)
{
	int err;

	if ((err = client_init_data()) < 0)
		return err;

	/* init memory, room for selected events */
	if ((err = snd_sequencer_memory_init()) < 0)
		return err;

	/* init event queues */
	if ((err = snd_seq_queues_init()) < 0)
		return err;

	/* register sequencer device */
	if ((err = snd_sequencer_device_init()) < 0)
		return err;

	/* register proc interface */
	if ((err = snd_seq_info_init()) < 0)
		return err;

	/* register our internal client */
	if ((err = snd_seq_system_client_init()) < 0)
		return err;

	return 0;
}

static void __exit alsa_seq_exit(void)
{
	/* unregister our internal client */
	snd_seq_system_client_done();

	/* unregister proc interface */
	snd_seq_info_done();
	
	/* delete timing queues */
	snd_seq_queues_delete();

	/* unregister sequencer device */
	snd_sequencer_device_done();

	/* release event memory */
	snd_sequencer_memory_done();
}

module_init(alsa_seq_init)
module_exit(alsa_seq_exit)

  /* seq_lock.c */
EXPORT_SYMBOL(snd_seq_sleep_in_lock);
EXPORT_SYMBOL(snd_seq_sleep_timeout_in_lock);
  /* seq_clientmgr.c */
EXPORT_SYMBOL(snd_seq_create_kernel_client);
EXPORT_SYMBOL(snd_seq_delete_kernel_client);
EXPORT_SYMBOL(snd_seq_kernel_client_enqueue);
EXPORT_SYMBOL(snd_seq_kernel_client_enqueue_blocking);
EXPORT_SYMBOL(snd_seq_kernel_client_dispatch);
EXPORT_SYMBOL(snd_seq_kernel_client_ctl);
EXPORT_SYMBOL(snd_seq_kernel_client_write_poll);
  /* seq_memory.c */
EXPORT_SYMBOL(snd_seq_expand_var_event);
EXPORT_SYMBOL(snd_seq_dump_var_event);
  /* seq_ports.c */
EXPORT_SYMBOL(snd_port_init_callback);
EXPORT_SYMBOL(snd_port_alloc_callback);
EXPORT_SYMBOL(snd_seq_event_port_attach);
EXPORT_SYMBOL(snd_seq_event_port_detach);
