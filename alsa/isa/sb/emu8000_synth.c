/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *     and (c) 1999 Steve Ratcliffe <steve@parabola.demon.co.uk>
 *  Copyright (C) 1999-2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Emu8000 synth plug-in routine
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
 */

#define SNDRV_MAIN_OBJECT_FILE
#include "emu8000_local.h"
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;
MODULE_CLASSES("{sound}");
MODULE_AUTHOR("Takashi Iwai, Steve Ratcliffe");

/*----------------------------------------------------------------*/

/*
 * create a new hardware dependent device for Emu8000
 */
static int snd_emu8000_new_device(snd_seq_device_t *dev)
{
	emu8000_t *hw;
	snd_emux_t *emu;

	hw = *(emu8000_t**)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (hw == NULL)
		return -EINVAL;

	if (hw->emu)
		return -EBUSY; /* already exists..? */

	if (snd_emux_new(&emu) < 0)
		return -ENOMEM;

	hw->emu = emu;
	snd_emu8000_ops_setup(hw);

	emu->hw = hw;
	emu->max_voices = EMU8000_DRAM_VOICES;
	emu->num_ports = hw->seq_ports;

	if (hw->memhdr) {
		snd_printk("memhdr is already initialized!?\n");
		snd_util_memhdr_free(hw->memhdr);
	}
	hw->memhdr = snd_util_memhdr_new(hw->mem_size);
	if (hw->memhdr == NULL) {
		snd_emux_free(emu);
		hw->emu = NULL;
		return -ENOMEM;
	}

	emu->memhdr = hw->memhdr;
	emu->midi_ports = hw->seq_ports < 2 ? hw->seq_ports : 2; /* number of virmidi ports */
	emu->midi_devidx = 1;

	if (snd_emux_register(emu, dev->card, hw->index, "Emu8000") < 0) {
		snd_emux_free(emu);
		snd_util_memhdr_free(hw->memhdr);
		hw->emu = NULL;
		hw->memhdr = NULL;
		return -ENOMEM;
	}

	dev->driver_data = hw;

	return 0;
}


/*
 * free all resources
 */
static int snd_emu8000_delete_device(snd_seq_device_t *dev)
{
	emu8000_t *hw;

	if (dev->driver_data == NULL)
		return 0; /* no synth was allocated actually */

	hw = dev->driver_data;
	if (hw->emu)
		snd_emux_free(hw->emu);
	if (hw->memhdr)
		snd_util_memhdr_free(hw->memhdr);
	hw->emu = NULL;
	hw->memhdr = NULL;
	return 0;
}

/*
 *  INIT part
 */

static int __init alsa_emu8000_init(void)
{
	
	static snd_seq_dev_ops_t ops = {
		snd_emu8000_new_device,
		snd_emu8000_delete_device,
	};
	return snd_seq_device_register_driver(SNDRV_SEQ_DEV_ID_EMU8000, &ops, sizeof(emu8000_t*));
}

static void __exit alsa_emu8000_exit(void)
{
	snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_EMU8000);
}

module_init(alsa_emu8000_init)
module_exit(alsa_emu8000_exit)
