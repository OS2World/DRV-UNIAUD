#ifndef __SEQ_VIRMIDI_H
#define __SEQ_VIRMIDI_H

/*
 *  Virtual Raw MIDI client on Sequencer
 *  Copyright (c) 2000 by Takashi Iwai <tiwai@suse.de>,
 *                        Jaroslav Kysela <perex@suse.cz>
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

#include "rawmidi.h"
#include "seq_midi_event.h"

typedef struct _snd_virmidi_dev snd_virmidi_dev_t;

/*
 * device file instance:
 * This instance is created at each time the midi device file is
 * opened.  Each instance has its own input buffer and MIDI parser
 * (buffer), and is associated with the device instance.
 */
typedef struct _snd_virmidi {
	struct list_head list;
	int seq_mode;
	int client;
	int port;
	int trigger: 1;
	snd_midi_event_t *parser;
	snd_seq_event_t event;
	snd_virmidi_dev_t *rdev;
	snd_rawmidi_substream_t *substream;
} snd_virmidi_t;

#define SNDRV_VIRMIDI_SUBSCRIBE	(1<<0)
#define SNDRV_VIRMIDI_USE		(1<<1)

/*
 * device record:
 * Each virtual midi device has one device instance.  It contains
 * common information and the linked-list of opened files, 
 */
struct _snd_virmidi_dev {
	snd_card_t *card;		/* associated card */
	snd_rawmidi_t *rmidi;		/* rawmidi device */
	int seq_mode;			/* SNDRV_VIRMIDI_XXX */
	int device;			/* sequencer device */
	int client;			/* created/attached client */
	int port;			/* created/attached port */
	unsigned int flags;		/* SNDRV_VIRMIDI_* */
	rwlock_t filelist_lock;
	struct list_head filelist;
};

/* sequencer mode:
 * ATTACH = input/output events from midi device are routed to the
 *          attached sequencer port.  sequencer port is not created
 *          by virmidi itself.
 * DISPATCH = input/output events are routed to subscribers.
 *            sequencer port is created in virmidi.
 */
#define SNDRV_VIRMIDI_SEQ_NONE		0
#define SNDRV_VIRMIDI_SEQ_ATTACH		1
#define SNDRV_VIRMIDI_SEQ_DISPATCH	2

int snd_virmidi_new(snd_card_t *card, int device, snd_rawmidi_t **rrmidi);

#endif
