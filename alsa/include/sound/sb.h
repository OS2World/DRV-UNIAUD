#ifndef __SB_H
#define __SB_H

/*
 *  Header file for SoundBlaster cards
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#include "pcm.h"
#include "rawmidi.h"

enum sb_hw_type {
	SB_HW_AUTO,
	SB_HW_10,
	SB_HW_20,
	SB_HW_201,
	SB_HW_PRO,
	SB_HW_16,
	SB_HW_16CSP,		/* SB16 with CSP chip */
	SB_HW_ALS100,		/* Avance Logic ALS100 chip */
	SB_HW_ALS4000,		/* Avance Logic ALS4000 chip */
};

#define SB_OPEN_PCM		0x01
#define SB_OPEN_MIDI_INPUT	0x02
#define SB_OPEN_MIDI_OUTPUT	0x04
#define SB_OPEN_MIDI_TRIGGER	0x08

#define SB_MODE_HALT		0x00
#define SB_MODE_PLAYBACK_8	0x01
#define SB_MODE_PLAYBACK_16	0x02
#define SB_MODE_PLAYBACK	(SB_MODE_PLAYBACK_8 | SB_MODE_PLAYBACK_16)
#define SB_MODE_CAPTURE_8	0x04
#define SB_MODE_CAPTURE_16	0x08
#define SB_MODE_CAPTURE		(SB_MODE_CAPTURE_8 | SB_MODE_CAPTURE_16)

#define SB_RATE_LOCK_PLAYBACK	0x10
#define SB_RATE_LOCK_CAPTURE	0x20
#define SB_RATE_LOCK		(SB_RATE_LOCK_PLAYBACK | SB_RATE_LOCK_CAPTURE)

#define SB_MPU_INPUT		1

struct _snd_sb {
	unsigned long port;		/* base port of DSP chip */
	struct resource *res_port;
	unsigned long alt_port;		/* alternate port (ALS4000) */
	struct resource *res_alt_port;
	unsigned long mpu_port;		/* MPU port for SB DSP 4.0+ */
	int irq;			/* IRQ number of DSP chip */
	int dma8;			/* 8-bit DMA */
	int dma16;			/* 16-bit DMA */
	unsigned short version;		/* version of DSP chip */
	enum sb_hw_type hardware;	/* see to SB_HW_XXXX */

	struct pci_dev *pci;		/* ALS4000 */

	unsigned int open;		/* see to SB_OPEN_XXXX for sb8 */
					/* also SNDRV_SB_CSP_MODE_XXX for sb16_csp */
	unsigned int mode;		/* current mode of stream */
	unsigned int force_mode16;	/* force 16-bit mode of streams */
	unsigned int locked_rate;	/* sb16 duplex */
	unsigned int playback_format;
	unsigned int capture_format;
	struct timer_list midi_timer;
	unsigned int p_dma_size;
	unsigned int p_period_size;
	unsigned int c_dma_size;
	unsigned int c_period_size;

	spinlock_t mixer_lock;

	char name[32];

#ifdef CONFIG_SND_SB16_CSP
	void *csp;
#endif

	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_pcm_substream_t *playback_substream;
	snd_pcm_substream_t *capture_substream;

	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *midi_substream_input;
	snd_rawmidi_substream_t *midi_substream_output;

	spinlock_t reg_lock;
	spinlock_t open_lock;
	spinlock_t midi_input_lock;

	snd_info_entry_t *proc_entry;
};

typedef struct _snd_sb sb_t;

/* I/O ports */

#define SBP(chip, x)		((chip)->port + s_b_SB_##x)
#define SBP1(port, x)		((port) + s_b_SB_##x)

#define s_b_SB_RESET		0x6
#define s_b_SB_READ		0xa
#define s_b_SB_WRITE		0xc
#define s_b_SB_COMMAND		0xc
#define s_b_SB_STATUS		0xc
#define s_b_SB_DATA_AVAIL	0xe
#define s_b_SB_DATA_AVAIL_16 	0xf
#define s_b_SB_MIXER_ADDR	0x4
#define s_b_SB_MIXER_DATA	0x5
#define s_b_SB_OPL3_LEFT	0x0
#define s_b_SB_OPL3_RIGHT	0x2
#define s_b_SB_OPL3_BOTH	0x8

#define SB_DSP_OUTPUT		0x14
#define SB_DSP_INPUT		0x24
#define SB_DSP_BLOCK_SIZE	0x48
#define SB_DSP_HI_OUTPUT	0x91
#define SB_DSP_HI_INPUT		0x99
#define SB_DSP_LO_OUTPUT_AUTO	0x1c
#define SB_DSP_LO_INPUT_AUTO	0x2c
#define SB_DSP_HI_OUTPUT_AUTO	0x90
#define SB_DSP_HI_INPUT_AUTO	0x98
#define SB_DSP_IMMED_INT	0xf2
#define SB_DSP_GET_VERSION	0xe1
#define SB_DSP_SPEAKER_ON	0xd1
#define SB_DSP_SPEAKER_OFF	0xd3
#define SB_DSP_DMA8_OFF		0xd0
#define SB_DSP_DMA8_ON		0xd4
#define SB_DSP_DMA8_EXIT	0xda
#define SB_DSP_DMA16_OFF	0xd5
#define SB_DSP_DMA16_ON		0xd6
#define SB_DSP_DMA16_EXIT	0xd9
#define SB_DSP_SAMPLE_RATE	0x40
#define SB_DSP_SAMPLE_RATE_OUT	0x41
#define SB_DSP_SAMPLE_RATE_IN	0x42
#define SB_DSP_MONO_8BIT	0xa0
#define SB_DSP_MONO_16BIT	0xa4
#define SB_DSP_STEREO_8BIT	0xa8
#define SB_DSP_STEREO_16BIT	0xac

#define SB_DSP_MIDI_INPUT_IRQ	0x31
#define SB_DSP_MIDI_OUTPUT	0x38

#define SB_DSP4_OUT8_AI		0xc6
#define SB_DSP4_IN8_AI		0xce
#define SB_DSP4_OUT16_AI	0xb6
#define SB_DSP4_IN16_AI		0xbe
#define SB_DSP4_MODE_UNS_MONO	0x00
#define SB_DSP4_MODE_SIGN_MONO	0x10
#define SB_DSP4_MODE_UNS_STEREO	0x20
#define SB_DSP4_MODE_SIGN_STEREO 0x30

#define SB_DSP4_OUTPUT		0x3c
#define SB_DSP4_INPUT_LEFT	0x3d
#define SB_DSP4_INPUT_RIGHT	0x3e

/* registers for SB 2.0 mixer */
#define SB_DSP20_MASTER_DEV	0x02
#define SB_DSP20_PCM_DEV	0x0A
#define SB_DSP20_CD_DEV		0x08
#define SB_DSP20_FM_DEV		0x06

/* registers for SB PRO mixer */
#define SB_DSP_MASTER_DEV	0x22
#define SB_DSP_PCM_DEV		0x04
#define SB_DSP_LINE_DEV		0x2e
#define SB_DSP_CD_DEV		0x28
#define SB_DSP_FM_DEV		0x26
#define SB_DSP_MIC_DEV		0x0a
#define SB_DSP_CAPTURE_SOURCE	0x0c
#define SB_DSP_CAPTURE_FILT	0x0c
#define SB_DSP_PLAYBACK_FILT	0x0e
#define SB_DSP_STEREO_SW	0x0e

#define SB_DSP_MIXS_MIC0	0x00	/* same as MIC */
#define SB_DSP_MIXS_CD		0x01
#define SB_DSP_MIXS_MIC		0x02
#define SB_DSP_MIXS_LINE	0x03

/* registers (only for left channel) for SB 16 mixer */
#define SB_DSP4_MASTER_DEV	0x30
#define SB_DSP4_BASS_DEV	0x46
#define SB_DSP4_TREBLE_DEV	0x44
#define SB_DSP4_SYNTH_DEV	0x34
#define SB_DSP4_PCM_DEV		0x32
#define SB_DSP4_SPEAKER_DEV	0x3b
#define SB_DSP4_LINE_DEV	0x38
#define SB_DSP4_MIC_DEV		0x3a
#define SB_DSP4_OUTPUT_SW	0x3c
#define SB_DSP4_CD_DEV		0x36
#define SB_DSP4_IGAIN_DEV	0x3f
#define SB_DSP4_OGAIN_DEV	0x41
#define SB_DSP4_MIC_AGC		0x43

/* additional registers for SB 16 mixer */
#define SB_DSP4_IRQSETUP	0x80
#define SB_DSP4_DMASETUP	0x81
#define SB_DSP4_IRQSTATUS	0x82
#define SB_DSP4_MPUSETUP	0x84

#define SB_DSP4_3DSE		0x90

/* IRQ setting bitmap */
#define SB_IRQSETUP_IRQ9	0x01
#define SB_IRQSETUP_IRQ5	0x02
#define SB_IRQSETUP_IRQ7	0x04
#define SB_IRQSETUP_IRQ10	0x08

/* IRQ types */
#define SB_IRQTYPE_8BIT		0x01
#define SB_IRQTYPE_16BIT	0x02
#define SB_IRQTYPE_MPUIN	0x04

/* DMA setting bitmap */
#define SB_DMASETUP_DMA0	0x01
#define SB_DMASETUP_DMA1	0x02
#define SB_DMASETUP_DMA3	0x08
#define SB_DMASETUP_DMA5	0x20
#define SB_DMASETUP_DMA6	0x40
#define SB_DMASETUP_DMA7	0x80

/*
 *
 */

inline void snd_sb_ack_8bit(sb_t *chip)
{
	inb(SBP(chip, DATA_AVAIL));
}

inline void snd_sb_ack_16bit(sb_t *chip)
{
	inb(SBP(chip, DATA_AVAIL_16));
}

/* sb_common.c */
int snd_sbdsp_command(sb_t *chip, unsigned char val);
int snd_sbdsp_get_byte(sb_t *chip);
int snd_sbdsp_reset(sb_t *chip);
int snd_sbdsp_create(snd_card_t *card,
		     unsigned long port,
		     int irq,
		     void (*irq_handler)(int, void *, struct pt_regs *),
		     int dma8, int dma16,
		     unsigned short hardware,
		     sb_t **r_chip);
/* sb_mixer.c */
void snd_sbmixer_write(sb_t *chip, unsigned char reg, unsigned char data);
unsigned char snd_sbmixer_read(sb_t *chip, unsigned char reg);
int snd_sbmixer_new(sb_t *chip);

/* sb8_init.c */
int snd_sb8dsp_pcm(sb_t *chip, int device, snd_pcm_t ** rpcm);
/* sb8.c */
void snd_sb8dsp_interrupt(sb_t *chip);
int snd_sb8_playback_open(snd_pcm_substream_t *substream);
int snd_sb8_capture_open(snd_pcm_substream_t *substream);
int snd_sb8_playback_close(snd_pcm_substream_t *substream);
int snd_sb8_capture_close(snd_pcm_substream_t *substream);
/* midi8.c */
void snd_sb8dsp_midi_interrupt(sb_t *chip);
int snd_sb8dsp_midi(sb_t *chip, int device, snd_rawmidi_t ** rrawmidi);

/* sb16_init.c */
int snd_sb16dsp_pcm(sb_t *chip, int device, snd_pcm_t ** rpcm);
int snd_sb16dsp_configure(sb_t *chip);
/* sb16.c */
void snd_sb16dsp_interrupt(int irq, void *dev_id, struct pt_regs *regs);
int snd_sb16_playback_open(snd_pcm_substream_t *substream);
int snd_sb16_capture_open(snd_pcm_substream_t *substream);
int snd_sb16_playback_close(snd_pcm_substream_t *substream);
int snd_sb16_capture_close(snd_pcm_substream_t *substream);

#endif				/* __SB_H */
