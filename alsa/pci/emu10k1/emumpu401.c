/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for control of EMU10K1 MPU-401 in UART mode
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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

#define EMU10K1_MIDI_MODE_INPUT		(1<<0)
#define EMU10K1_MIDI_MODE_OUTPUT	(1<<1)

static inline unsigned char mpu401_read(emu10k1_t *emu, emu10k1_midi_t *mpu, int idx)
{
	if (emu->audigy)
		return (unsigned char)snd_emu10k1_ptr_read(emu, mpu->port + idx, 0);
	else
		return inb(emu->port + mpu->port + idx);
}

static inline void mpu401_write(emu10k1_t *emu, emu10k1_midi_t *mpu, int data, int idx)
{
	if (emu->audigy)
		snd_emu10k1_ptr_write(emu, mpu->port + idx, 0, data);
	else
		outb(data, emu->port + mpu->port + idx);
}

#define mpu401_write_data(emu, mpu, data)	mpu401_write(emu, mpu, data, 0)
#define mpu401_write_cmd(emu, mpu, data)	mpu401_write(emu, mpu, data, 1)
#define mpu401_read_data(emu, mpu)		mpu401_read(emu, mpu, 0)
#define mpu401_read_stat(emu, mpu)		mpu401_read(emu, mpu, 1)

#define mpu401_input_avail(emu,mpu)	(!(mpu401_read_stat(emu,mpu) & 0x80))
#define mpu401_output_ready(emu,mpu)	(!(mpu401_read_stat(emu,mpu) & 0x40))

#define MPU401_RESET		0xff
#define MPU401_ENTER_UART	0x3f
#define MPU401_ACK		0xfe

static void mpu401_clear_rx(emu10k1_t *emu, emu10k1_midi_t *mpu)
{
	int timeout = 100000;
	for (; timeout > 0 && mpu401_input_avail(emu, mpu); timeout--)
		mpu401_read_data(emu, mpu);
#ifdef CONFIG_SND_DEBUG
	if (timeout <= 0)
		snd_printk("cmd: clear rx timeout (status = 0x%x)\n", mpu401_read_stat(emu, mpu));
#endif
}

/*

 */

static void do_emu10k1_midi_interrupt(emu10k1_t *emu, emu10k1_midi_t *midi, unsigned int status)
{
	unsigned char byte;

	if (midi->rmidi == NULL) {
		snd_emu10k1_intr_disable(emu, midi->tx_enable | midi->rx_enable);
		return;
	}

	spin_lock(&midi->input_lock);
	if ((status & midi->ipr_rx) && mpu401_input_avail(emu, midi)) {
		if (!(midi->midi_mode & EMU10K1_MIDI_MODE_INPUT)) {
			mpu401_clear_rx(emu, midi);
		} else {
			byte = mpu401_read_data(emu, midi);
			spin_unlock(&midi->input_lock);
			if (midi->substream_input)
				snd_rawmidi_receive(midi->substream_input, &byte, 1);
			spin_lock(&midi->input_lock);
		}
	}
	spin_unlock(&midi->input_lock);

	spin_lock(&midi->output_lock);
	if ((status & midi->ipr_tx) && mpu401_output_ready(emu, midi)) {
		if (midi->substream_output &&
		    snd_rawmidi_transmit(midi->substream_output, &byte, 1) == 1) {
			mpu401_write_data(emu, midi, byte);
		} else {
			snd_emu10k1_intr_disable(emu, midi->tx_enable);
		}
	}
	spin_unlock(&midi->output_lock);
}

static void snd_emu10k1_midi_interrupt(emu10k1_t *emu, unsigned int status)
{
	do_emu10k1_midi_interrupt(emu, &emu->midi, status);
}

static void snd_emu10k1_midi_interrupt2(emu10k1_t *emu, unsigned int status)
{
	do_emu10k1_midi_interrupt(emu, &emu->midi2, status);
}

static void snd_emu10k1_midi_cmd(emu10k1_t * emu, emu10k1_midi_t *midi, unsigned char cmd, int ack)
{
	unsigned long flags;
	int timeout, ok;

	spin_lock_irqsave(&midi->input_lock, flags);
	mpu401_write_data(emu, midi, 0x00);
	/* mpu401_clear_rx(emu, midi); */

	mpu401_write_cmd(emu, midi, cmd);
	if (ack) {
		ok = 0;
		timeout = 10000;
		while (!ok && timeout-- > 0) {
			if (mpu401_input_avail(emu, midi)) {
				if (mpu401_read_data(emu, midi) == MPU401_ACK)
					ok = 1;
			}
		}
		if (!ok && mpu401_read_data(emu, midi) == MPU401_ACK)
			ok = 1;
	} else {
		ok = 1;
	}
	spin_unlock_irqrestore(&midi->input_lock, flags);
	if (!ok)
		snd_printk("midi_cmd: 0x%x failed at 0x%lx (status = 0x%x, data = 0x%x)!!!\n",
			   cmd, emu->port,
			   mpu401_read_stat(emu, midi),
			   mpu401_read_data(emu, midi));
}

static int snd_emu10k1_midi_input_open(snd_rawmidi_substream_t * substream)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = (emu10k1_midi_t *)substream->rmidi->private_data;
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	midi->midi_mode |= EMU10K1_MIDI_MODE_INPUT;
	midi->substream_input = substream;
	if (!(midi->midi_mode & EMU10K1_MIDI_MODE_OUTPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		snd_emu10k1_midi_cmd(emu, midi, MPU401_RESET, 1);
		snd_emu10k1_midi_cmd(emu, midi, MPU401_ENTER_UART, 1);
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;
}

static int snd_emu10k1_midi_output_open(snd_rawmidi_substream_t * substream)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = (emu10k1_midi_t *)substream->rmidi->private_data;
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	midi->midi_mode |= EMU10K1_MIDI_MODE_OUTPUT;
	midi->substream_output = substream;
	if (!(midi->midi_mode & EMU10K1_MIDI_MODE_INPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		snd_emu10k1_midi_cmd(emu, midi, MPU401_RESET, 1);
		snd_emu10k1_midi_cmd(emu, midi, MPU401_ENTER_UART, 1);
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;
}

static int snd_emu10k1_midi_input_close(snd_rawmidi_substream_t * substream)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = (emu10k1_midi_t *)substream->rmidi->private_data;
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	snd_emu10k1_intr_disable(emu, midi->rx_enable);
	midi->midi_mode &= ~EMU10K1_MIDI_MODE_INPUT;
	midi->substream_input = NULL;
	if (!(midi->midi_mode & EMU10K1_MIDI_MODE_OUTPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		snd_emu10k1_midi_cmd(emu, midi, MPU401_RESET, 0);
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;
}

static int snd_emu10k1_midi_output_close(snd_rawmidi_substream_t * substream)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = (emu10k1_midi_t *)substream->rmidi->private_data;
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	snd_emu10k1_intr_disable(emu, midi->tx_enable);
	midi->midi_mode &= ~EMU10K1_MIDI_MODE_OUTPUT;
	midi->substream_output = NULL;
	if (!(midi->midi_mode & EMU10K1_MIDI_MODE_INPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		snd_emu10k1_midi_cmd(emu, midi, MPU401_RESET, 0);
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;
}

static void snd_emu10k1_midi_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = (emu10k1_midi_t *)substream->rmidi->private_data;
	emu = midi->emu;
	snd_assert(emu, return);

	if (up)
		snd_emu10k1_intr_enable(emu, midi->rx_enable);
	else
		snd_emu10k1_intr_disable(emu, midi->rx_enable);
}

static void snd_emu10k1_midi_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = (emu10k1_midi_t *)substream->rmidi->private_data;
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return);

	if (up) {
		int max = 4;
		unsigned char byte;
	
		/* try to send some amount of bytes here before interrupts */
		spin_lock_irqsave(&midi->output_lock, flags);
		while (max > 0) {
			if (mpu401_output_ready(emu, midi)) {
				if (!(midi->midi_mode & EMU10K1_MIDI_MODE_OUTPUT) ||
				    snd_rawmidi_transmit(substream, &byte, 1) != 1) {
					/* no more data */
					spin_unlock_irqrestore(&midi->output_lock, flags);
					return;
				}
				mpu401_write_data(emu, midi, byte);
				max--;
			} else {
				break;
			}
		}
		spin_unlock_irqrestore(&midi->output_lock, flags);
		snd_emu10k1_intr_enable(emu, midi->tx_enable);
	} else {
		snd_emu10k1_intr_disable(emu, midi->tx_enable);
	}
}

/*

 */

#ifdef TARGET_OS2
static snd_rawmidi_ops_t snd_emu10k1_midi_output =
{
	snd_emu10k1_midi_output_open,
	snd_emu10k1_midi_output_close,
	snd_emu10k1_midi_output_trigger,0
};

static snd_rawmidi_ops_t snd_emu10k1_midi_input =
{
	snd_emu10k1_midi_input_open,
	snd_emu10k1_midi_input_close,
	snd_emu10k1_midi_input_trigger,0
};
#else
static snd_rawmidi_ops_t snd_emu10k1_midi_output =
{
	open:		snd_emu10k1_midi_output_open,
	close:		snd_emu10k1_midi_output_close,
	trigger:	snd_emu10k1_midi_output_trigger,
};

static snd_rawmidi_ops_t snd_emu10k1_midi_input =
{
	open:		snd_emu10k1_midi_input_open,
	close:		snd_emu10k1_midi_input_close,
	trigger:	snd_emu10k1_midi_input_trigger,
};
#endif

static void snd_emu10k1_midi_free(snd_rawmidi_t *rmidi)
{
	emu10k1_midi_t *midi = (emu10k1_midi_t *)rmidi->private_data;
	midi->interrupt = NULL;
	midi->rmidi = NULL;
}

static int __devinit emu10k1_midi_init(emu10k1_t *emu, emu10k1_midi_t *midi, int device, char *name)
{
	snd_rawmidi_t *rmidi;
	int err;

	if ((err = snd_rawmidi_new(emu->card, name, device, 1, 1, &rmidi)) < 0)
		return err;
	midi->emu = emu;
	spin_lock_init(&midi->open_lock);
	spin_lock_init(&midi->input_lock);
	spin_lock_init(&midi->output_lock);
	strcpy(rmidi->name, name);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_emu10k1_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_emu10k1_midi_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
	                     SNDRV_RAWMIDI_INFO_INPUT |
	                     SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = midi;
	rmidi->private_free = snd_emu10k1_midi_free;
	midi->rmidi = rmidi;
	return 0;
}

int __devinit snd_emu10k1_midi(emu10k1_t *emu)
{
	emu10k1_midi_t *midi = &emu->midi;
	int err;

	if ((err = emu10k1_midi_init(emu, midi, 0, "EMU10K1 MPU-401 (UART)")) < 0)
		return err;

	midi->tx_enable = INTE_MIDITXENABLE;
	midi->rx_enable = INTE_MIDIRXENABLE;
	midi->port = MUDATA;
	midi->ipr_tx = IPR_MIDITRANSBUFEMPTY;
	midi->ipr_rx = IPR_MIDIRECVBUFEMPTY;
	midi->interrupt = snd_emu10k1_midi_interrupt;
	return 0;
}

int __devinit snd_emu10k1_audigy_midi(emu10k1_t *emu)
{
	emu10k1_midi_t *midi;
	int err;

	midi = &emu->midi;
	if ((err = emu10k1_midi_init(emu, midi, 0, "Audigy MPU-401 (UART)")) < 0)
		return err;

	midi->tx_enable = INTE_MIDITXENABLE;
	midi->rx_enable = INTE_MIDIRXENABLE;
	midi->port = A_MUDATA1;
	midi->ipr_tx = IPR_MIDITRANSBUFEMPTY;
	midi->ipr_rx = IPR_MIDIRECVBUFEMPTY;
	midi->interrupt = snd_emu10k1_midi_interrupt;

	midi = &emu->midi2;
	if ((err = emu10k1_midi_init(emu, midi, 1, "Audigy MPU-401 #2")) < 0)
		return err;

	midi->tx_enable = INTE_A_MIDITXENABLE2;
	midi->rx_enable = INTE_A_MIDIRXENABLE2;
	midi->port = A_MUDATA2;
	midi->ipr_tx = IPR_A_MIDITRANSBUFEMPTY2;
	midi->ipr_rx = IPR_A_MIDIRECVBUFEMPTY2;
	midi->interrupt = snd_emu10k1_midi_interrupt2;
	return 0;
}
