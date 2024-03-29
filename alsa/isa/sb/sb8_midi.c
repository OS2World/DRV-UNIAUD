/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for control of SoundBlaster cards - MIDI interface
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
 * --
 *
 * Sun May  9 22:54:38 BST 1999 George David Morrison <gdm@gedamo.demon.co.uk>
 *   Fixed typo in snd_sb8dsp_midi_new_device which prevented midi from 
 *   working.
 */

#include <sound/driver.h>
#include <sound/sb.h>

/*

 */

void snd_sb8dsp_midi_interrupt(sb_t * chip)
{
	snd_rawmidi_t *rmidi;
	int max = 64;
	char byte;

	if (chip == NULL || (rmidi = chip->rmidi) == NULL) {
		inb(SBP(chip, READ));	/* ack interrupt */
		return;
	}
	while (max-- > 0) {
		spin_lock(&chip->midi_input_lock);
		if (inb(SBP(chip, DATA_AVAIL)) & 0x80) {
			byte = inb(SBP(chip, READ));
			spin_unlock(&chip->midi_input_lock);
			snd_rawmidi_receive(chip->midi_substream_input, &byte, 1);
		} else {
			spin_unlock(&chip->midi_input_lock);
		}
	}
}

/*

 */

static int snd_sb8dsp_midi_input_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	sb_t *chip;

	chip = snd_magic_cast(sb_t, substream->rmidi->private_data, return -ENXIO);
	spin_lock_irqsave(&chip->open_lock, flags);
	if (chip->open) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		return -EAGAIN;
	}
	chip->open |= SB_OPEN_MIDI_INPUT;
	chip->midi_substream_input = substream;
	if (!(chip->open & SB_OPEN_MIDI_OUTPUT)) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		snd_sbdsp_reset(chip);		/* reset DSP */
	} else {
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
	return 0;
}

static int snd_sb8dsp_midi_output_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	sb_t *chip;

	chip = snd_magic_cast(sb_t, substream->rmidi->private_data, return -ENXIO);
	spin_lock_irqsave(&chip->open_lock, flags);
	if (chip->open) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		return -EAGAIN;
	}
	chip->open |= SB_OPEN_MIDI_OUTPUT;
	chip->midi_substream_output = substream;
	if (!(chip->open & SB_OPEN_MIDI_INPUT)) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		snd_sbdsp_reset(chip);		/* reset DSP */
	} else {
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
	return 0;
}

static int snd_sb8dsp_midi_input_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	sb_t *chip;

	chip = snd_magic_cast(sb_t, substream->rmidi->private_data, return -ENXIO);
	spin_lock_irqsave(&chip->open_lock, flags);
	chip->open &= ~(SB_OPEN_MIDI_INPUT | SB_OPEN_MIDI_TRIGGER);
	chip->midi_substream_input = NULL;
	if (!(chip->open & SB_OPEN_MIDI_OUTPUT)) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		snd_sbdsp_reset(chip);		/* reset DSP */
	} else {
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
	return 0;
}

static int snd_sb8dsp_midi_output_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	sb_t *chip;

	chip = snd_magic_cast(sb_t, substream->rmidi->private_data, return -ENXIO);
	spin_lock_irqsave(&chip->open_lock, flags);
	chip->open &= ~SB_OPEN_MIDI_OUTPUT;
	chip->midi_substream_output = NULL;
	if (!(chip->open & SB_OPEN_MIDI_INPUT)) {
		spin_unlock_irqrestore(&chip->open_lock, flags);
		snd_sbdsp_reset(chip);		/* reset DSP */
	} else {
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
	return 0;
}

static void snd_sb8dsp_midi_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	sb_t *chip;

	chip = snd_magic_cast(sb_t, substream->rmidi->private_data, return);
	spin_lock_irqsave(&chip->open_lock, flags);
	if (up) {
		if (!(chip->open & SB_OPEN_MIDI_TRIGGER)) {
			snd_sbdsp_command(chip, SB_DSP_MIDI_INPUT_IRQ);
			chip->open |= SB_OPEN_MIDI_TRIGGER;
		}
	} else {
		if (chip->open & SB_OPEN_MIDI_TRIGGER) {
			snd_sbdsp_command(chip, SB_DSP_MIDI_INPUT_IRQ);
			chip->open &= ~SB_OPEN_MIDI_TRIGGER;
		}
	}
	spin_unlock_irqrestore(&chip->open_lock, flags);
}

static void snd_sb8dsp_midi_output_write(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	sb_t *chip;
	char byte;
	int max = 32;

	/* how big is Tx FIFO? */
	chip = snd_magic_cast(sb_t, substream->rmidi->private_data, return);
	while (max-- > 0) {
		spin_lock_irqsave(&chip->open_lock, flags);
		if (snd_rawmidi_transmit(substream, &byte, 1) != 1) {
			chip->open &= ~SB_OPEN_MIDI_TRIGGER;
			del_timer(&chip->midi_timer);
			spin_unlock_irqrestore(&chip->open_lock, flags);
			return;
		}
		snd_sbdsp_command(chip, SB_DSP_MIDI_OUTPUT);
		snd_sbdsp_command(chip, byte);
		spin_unlock_irqrestore(&chip->open_lock, flags);
	}
}

static void snd_sb8dsp_midi_output_timer(unsigned long data)
{
	snd_rawmidi_substream_t * substream = (snd_rawmidi_substream_t *) data;
	sb_t * chip = snd_magic_cast(sb_t, substream->rmidi->private_data, return);
	unsigned long flags;

	spin_lock_irqsave(&chip->open_lock, flags);
	chip->midi_timer.expires = 1 + jiffies;
	add_timer(&chip->midi_timer);
	spin_unlock_irqrestore(&chip->open_lock, flags);	
	snd_sb8dsp_midi_output_write(substream);
}

static void snd_sb8dsp_midi_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	sb_t *chip;

	chip = snd_magic_cast(sb_t, substream->rmidi->private_data, return);
	spin_lock_irqsave(&chip->open_lock, flags);
	if (up) {
		if (!(chip->open & SB_OPEN_MIDI_TRIGGER)) {
			chip->midi_timer.function = snd_sb8dsp_midi_output_timer;
			chip->midi_timer.data = (unsigned long) substream;
			chip->midi_timer.expires = 1 + jiffies;
			add_timer(&chip->midi_timer);
			chip->open |= SB_OPEN_MIDI_TRIGGER;
		}
	} else {
		if (chip->open & SB_OPEN_MIDI_TRIGGER) {
			chip->open &= ~SB_OPEN_MIDI_TRIGGER;
		}
	}
	spin_unlock_irqrestore(&chip->open_lock, flags);

	if (up)
		snd_sb8dsp_midi_output_write(substream);
}

/*

 */

#ifdef TARGET_OS2
static snd_rawmidi_ops_t snd_sb8dsp_midi_output =
{
	snd_sb8dsp_midi_output_open,
	snd_sb8dsp_midi_output_close,
	snd_sb8dsp_midi_output_trigger,0
};

static snd_rawmidi_ops_t snd_sb8dsp_midi_input =
{
	snd_sb8dsp_midi_input_open,
	snd_sb8dsp_midi_input_close,
	snd_sb8dsp_midi_input_trigger,0
};
#else
static snd_rawmidi_ops_t snd_sb8dsp_midi_output =
{
	open:           snd_sb8dsp_midi_output_open,
	close:          snd_sb8dsp_midi_output_close,
	trigger:	snd_sb8dsp_midi_output_trigger,
};

static snd_rawmidi_ops_t snd_sb8dsp_midi_input =
{
	open:           snd_sb8dsp_midi_input_open,
	close:          snd_sb8dsp_midi_input_close,
	trigger:        snd_sb8dsp_midi_input_trigger,
};
#endif

int snd_sb8dsp_midi(sb_t *chip, int device, snd_rawmidi_t ** rrawmidi)
{
	snd_rawmidi_t *rmidi;
	int err;

	if (rrawmidi)
		*rrawmidi = NULL;
	if ((err = snd_rawmidi_new(chip->card, "SB8 MIDI", device, 1, 1, &rmidi)) < 0)
		return err;
	strcpy(rmidi->name, "SB8 MIDI");
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_sb8dsp_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_sb8dsp_midi_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = chip;
	chip->rmidi = rmidi;
	if (rrawmidi)
		*rrawmidi = rmidi;
	return 0;
}
