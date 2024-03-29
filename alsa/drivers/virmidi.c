/*
 *  Dummy soundcard for virtual rawmidi devices
 *
 *  Copyright (c) 2000 by Takashi Iwai <tiwai@suse.de>
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

/*
 * VIRTUAL RAW MIDI DEVICE CARDS
 *
 * This dummy card contains up to 4 virtual rawmidi devices.
 * They are not real rawmidi devices but just associated with sequencer
 * clients, so that any input/output sources can be connected as a raw
 * MIDI device arbitrary.
 * Also, multiple access is allowed to a single rawmidi device.
 *
 * Typical usage is like following:
 * - Load snd-card-virmidi module.
 *	# modprobe snd-card-virmidi snd_index=2
 *   Then, sequencer clients 72:0 to 75:0 will be created, which are
 *   mapped from /dev/snd/midiC1D0 to /dev/snd/midiC1D3, respectively.
 *
 * - Connect input/output via aconnect.
 *	% aconnect 64:0 72:0	# keyboard input redirection 64:0 -> 72:0
 *	% aconnect 72:0 65:0	# output device redirection 72:0 -> 65:0
 *
 * - Run application using a midi device (eg. /dev/snd/midiC1D0)
 */

#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#include <sound/seq_kernel.h>
#include <sound/seq_virmidi.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("Dummy soundcard for virtual rawmidi devices");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{ALSA,Virtual rawmidi device}}");

#define MAX_MIDI_DEVICES	8

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
#ifdef TARGET_OS2
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;
static int snd_midi_devs[SNDRV_CARDS] = SNDDRV_DEFAULT_MIDI_DEVS
#else
static int snd_enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};
static int snd_midi_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};
#endif

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for virmidi soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for virmidi soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable this soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_midi_devs, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_midi_devs, "MIDI devices # (1-8)");
MODULE_PARM_SYNTAX(snd_midi_devs, SNDRV_ENABLED ",allows:{{1,8}}");

typedef struct snd_card_virmidi {
	snd_card_t *card;
	snd_rawmidi_t *midi[MAX_MIDI_DEVICES];
} snd_card_virmidi_t;

static snd_card_t *snd_virmidi_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_card_virmidi_probe(int dev)
{
	snd_card_t *card;
	struct snd_card_virmidi *vmidi;
	int idx, err;

	if (!snd_enable[dev])
		return -ENODEV;
	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE,
			    sizeof(struct snd_card_virmidi));
	if (card == NULL)
		return -ENOMEM;
	vmidi = (struct snd_card_virmidi *)card->private_data;
	vmidi->card = card;

	if (snd_midi_devs[dev] > MAX_MIDI_DEVICES) {
		snd_printk("too much midi devices for virmidi %d: force to use %d\n", dev, MAX_MIDI_DEVICES);
		snd_midi_devs[dev] = MAX_MIDI_DEVICES;
	}
	for (idx = 0; idx < snd_midi_devs[dev]; idx++) {
		snd_rawmidi_t *rmidi;
		snd_virmidi_dev_t *rdev;
		if ((err = snd_virmidi_new(card, idx, &rmidi)) < 0)
			goto __nodev;
		rdev = snd_magic_cast(snd_virmidi_dev_t, rmidi->private_data, continue);
		vmidi->midi[idx] = rmidi;
		strcpy(rmidi->name, "Virtual Raw MIDI");
		rdev->seq_mode = SNDRV_VIRMIDI_SEQ_DISPATCH;
	}
	
	strcpy(card->driver, "VirMIDI");
	strcpy(card->shortname, "VirMIDI");
	sprintf(card->longname, "Virtual MIDI Card %i", dev + 1);
	if ((err = snd_card_register(card)) == 0) {
		snd_virmidi_cards[dev] = card;
		return 0;
	}
      __nodev:
	snd_card_free(card);
	return err;
}

static int __init alsa_card_virmidi_init(void)
{
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && snd_enable[dev]; dev++) {
		if (snd_card_virmidi_probe(dev) < 0) {
#ifdef MODULE
			snd_printk("Card-VirMIDI #%i not found or device busy\n", dev + 1);
#endif
			break;
		}
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		snd_printk("Card-VirMIDI soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_virmidi_exit(void)
{
	int dev;

	for (dev = 0; dev < SNDRV_CARDS; dev++)
		snd_card_free(snd_virmidi_cards[dev]);
}

module_init(alsa_card_virmidi_init)
module_exit(alsa_card_virmidi_exit)

#ifndef MODULE

/* format is: snd-card-virmidi=snd_enable,snd_index,snd_id,snd_midi_devs */

static int __init alsa_card_virmidi_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,&snd_midi_devs[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-virmidi=", alsa_card_virmidi_setup);

#endif /* ifndef MODULE */
