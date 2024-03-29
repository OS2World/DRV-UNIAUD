/* 
 * Driver for NeoMagic 256AV and 256ZX chipsets.
 * Copyright (c) 2000 by Takashi Iwai <tiwai@suse.de>
 *
 * Based on nm256_audio.c OSS driver in linux kernel.
 * The original author of OSS nm256 driver wishes to remain anonymous,
 * so I just put my acknoledgment to him/her here.
 * The original author's web page is found at
 *	http://www.uglx.org/sony.html
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
 */
  
#define SNDRV_MAIN_OBJECT_FILE

#include <sound/driver.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#define CARD_NAME "NeoMagic 256AV/ZX"
#define DRIVER_NAME "NM256"

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("NeoMagic NM256AV/ZX");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{NeoMagic,NM256AV},"
		"{NeoMagic,NM256ZX}}");

/*
 * some compile conditions.
 */

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;
#ifdef TARGET_OS2
static int snd_playback_bufsize[SNDRV_CARDS] = {REPEAT_SNDRV(16)};
static int snd_capture_bufsize[SNDRV_CARDS] = {REPEAT_SNDRV(16)};
static int snd_force_ac97[SNDRV_CARDS] = {REPEAT_SNDRV(0)}; /* disabled as default */
static int snd_buffer_top[SNDRV_CARDS] = {REPEAT_SNDRV(0)}; /* not specified */
static int snd_use_cache[SNDRV_CARDS] = {REPEAT_SNDRV(0)}; /* disabled */
static int snd_vaio_hack[SNDRV_CARDS] = {REPEAT_SNDRV(0)}; /* disabled */
#else
static int snd_playback_bufsize[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 16};
static int snd_capture_bufsize[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 16};
static int snd_force_ac97[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0}; /* disabled as default */
static int snd_buffer_top[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0}; /* not specified */
static int snd_use_cache[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0}; /* disabled */
static int snd_vaio_hack[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0}; /* disabled */
#endif

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable this soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_playback_bufsize, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_playback_bufsize, "DAC frame size in kB for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_playback_bufsize, SNDRV_ENABLED);
MODULE_PARM(snd_capture_bufsize, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_capture_bufsize, "ADC frame size in kB for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_capture_bufsize, SNDRV_ENABLED);
MODULE_PARM(snd_force_ac97, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_force_ac97, "Force to use AC97 codec for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_force_ac97, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);
MODULE_PARM(snd_buffer_top, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_buffer_top, "Set the top address of audio buffer for " CARD_NAME " soundcard.");
MODULE_PARM_SYNTAX(snd_buffer_top, SNDRV_ENABLED);
MODULE_PARM(snd_use_cache, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_use_cache, "Enable the cache for coefficient table access.");
MODULE_PARM_SYNTAX(snd_use_cache, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);
MODULE_PARM(snd_vaio_hack, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_vaio_hack, "Enable workaround for Sony VAIO notebooks.");
MODULE_PARM_SYNTAX(snd_vaio_hack, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);

/*
 * hw definitions
 */

/* The BIOS signature. */
#define NM_SIGNATURE 0x4e4d0000
/* Signature mask. */
#define NM_SIG_MASK 0xffff0000

/* Size of the second memory area. */
#define NM_PORT2_SIZE 4096

/* The base offset of the mixer in the second memory area. */
#define NM_MIXER_OFFSET 0x600

/* The maximum size of a coefficient entry. */
#define NM_MAX_COEFFICIENT 0x3000

/* The interrupt register. */
#define NM_INT_REG 0xa04
/* And its bits. */
#define SNDRV_PCM_STREAM_PLAYBACK_INT 0x40
#define NM_RECORD_INT 0x100
#define NM_MISC_INT_1 0x4000
#define NM_MISC_INT_2 0x1
#define NM_ACK_INT(chip, X) snd_nm256_writew(chip, NM_INT_REG, (X) << 1)

/* The AV's "mixer ready" status bit and location. */
#define NM_MIXER_STATUS_OFFSET 0xa04
#define NM_MIXER_READY_MASK 0x0800
#define NM_MIXER_PRESENCE 0xa06
#define NM_PRESENCE_MASK 0x0050
#define NM_PRESENCE_VALUE 0x0040

/*
 * For the ZX.  It uses the same interrupt register, but it holds 32
 * bits instead of 16.
 */
#define NM2_PLAYBACK_INT 0x10000
#define NM2_RECORD_INT 0x80000
#define NM2_MISC_INT_1 0x8
#define NM2_MISC_INT_2 0x2
#define NM2_ACK_INT(chip, X) snd_nm256_writel(chip, NM_INT_REG, (X))

/* The ZX's "mixer ready" status bit and location. */
#define NM2_MIXER_STATUS_OFFSET 0xa06
#define NM2_MIXER_READY_MASK 0x0800

/* The playback registers start from here. */
#define SNDRV_PCM_STREAM_PLAYBACK_REG_OFFSET 0x0
/* The record registers start from here. */
#define NM_RECORD_REG_OFFSET 0x200

/* The rate register is located 2 bytes from the start of the register area. */
#define NM_RATE_REG_OFFSET 2

/* Mono/stereo flag, number of bits on playback, and rate mask. */
#define NM_RATE_STEREO 1
#define NM_RATE_BITS_16 2
#define NM_RATE_MASK 0xf0

/* Playback enable register. */
#define SNDRV_PCM_STREAM_PLAYBACK_ENABLE_REG (SNDRV_PCM_STREAM_PLAYBACK_REG_OFFSET + 0x1)
#define SNDRV_PCM_STREAM_PLAYBACK_ENABLE_FLAG 1
#define SNDRV_PCM_STREAM_PLAYBACK_ONESHOT 2
#define SNDRV_PCM_STREAM_PLAYBACK_FREERUN 4

/* Mutes the audio output. */
#define NM_AUDIO_MUTE_REG (SNDRV_PCM_STREAM_PLAYBACK_REG_OFFSET + 0x18)
#define NM_AUDIO_MUTE_LEFT 0x8000
#define NM_AUDIO_MUTE_RIGHT 0x0080

/* Recording enable register. */
#define NM_RECORD_ENABLE_REG (NM_RECORD_REG_OFFSET + 0)
#define NM_RECORD_ENABLE_FLAG 1
#define NM_RECORD_FREERUN 2

/* coefficient buffer pointer */
#define NM_COEFF_START_OFFSET	0x1c
#define NM_COEFF_END_OFFSET	0x20

/* DMA buffer offsets */
#define NM_RBUFFER_START (NM_RECORD_REG_OFFSET + 0x4)
#define NM_RBUFFER_END   (NM_RECORD_REG_OFFSET + 0x10)
#define NM_RBUFFER_WMARK (NM_RECORD_REG_OFFSET + 0xc)
#define NM_RBUFFER_CURRP (NM_RECORD_REG_OFFSET + 0x8)

#define NM_PBUFFER_START (SNDRV_PCM_STREAM_PLAYBACK_REG_OFFSET + 0x4)
#define NM_PBUFFER_END   (SNDRV_PCM_STREAM_PLAYBACK_REG_OFFSET + 0x14)
#define NM_PBUFFER_WMARK (SNDRV_PCM_STREAM_PLAYBACK_REG_OFFSET + 0xc)
#define NM_PBUFFER_CURRP (SNDRV_PCM_STREAM_PLAYBACK_REG_OFFSET + 0x8)

/*
 * type definitions
 */

typedef struct snd_nm256 nm256_t;
typedef struct snd_nm256_stream nm256_stream_t;
#define chip_t nm256_t

struct snd_nm256_stream {

	nm256_t *chip;
	snd_pcm_substream_t *substream;
	int running;
	
	u32 buf;	/* offset from chip->buffer */
	int bufsize;	/* buffer size in kb */
	unsigned long bufptr;	/* mapped pointer */

	int dma_size;	/* in bytes */
	int period_size;	/* in bytes */
	int frags;	/* # of periods */
	int shift;	/* bit shifts */
	int cur_frag;	/* current period # */

};

struct snd_nm256 {
	
	snd_card_t *card;

	unsigned long cport;		/* control port */
	struct resource *res_cport;
	unsigned long cport_addr;	/* physical address */

	unsigned long buffer;			/* buffer */
	struct resource *res_buffer;
	unsigned long buffer_addr;	/* buffer phyiscal address */

	u32 buffer_start;		/* start offset from pci resource 0 */
	u32 buffer_end;			/* end offset */
	u32 buffer_size;

	u32 all_coeff_buf;
	u32 coeff_buf[2];		/* coefficient buffer */

	int coeffs_current;
	int use_cache;

	int mixer_base;
	int mixer_status_offset;
	int mixer_status_mask;

	int irq;
	void (*interrupt)(int, void *, struct pt_regs *);
	int badintrcount;

	nm256_stream_t streams[2];

	ac97_t *ac97;

	snd_pcm_t *pcm;

	struct pci_dev *pci;

	spinlock_t reg_lock;

};


/*
 * include coefficient table
 */
#include "nm256_coef.c"


/*
 * PCI ids
 */

#ifndef PCI_VENDOR_ID_NEOMAGIC
#define PCI_VENDOR_ID_NEOMEGIC 0x10c8
#endif
#ifndef PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO
#define PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO 0x8005
#endif
#ifndef PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO
#define PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO 0x8006
#endif


static struct pci_device_id snd_nm256_ids[] __devinitdata = {
	{PCI_VENDOR_ID_NEOMAGIC, PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NEOMAGIC, PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,},
};


/*
 * lowlvel stuffs
 */

inline static u8
snd_nm256_readb(nm256_t *chip, int offset)
{
	return readb(chip->cport + offset);
}

inline static u16
snd_nm256_readw(nm256_t *chip, int offset)
{
	return readw(chip->cport + offset);
}

inline static u32
snd_nm256_readl(nm256_t *chip, int offset)
{
	return readl(chip->cport + offset);
}

inline static void
snd_nm256_writeb(nm256_t *chip, int offset, u8 val)
{
	writeb(val, chip->cport + offset);
}

inline static void
snd_nm256_writew(nm256_t *chip, int offset, u16 val)
{
	writew(val, chip->cport + offset);
}

inline static void
snd_nm256_writel(nm256_t *chip, int offset, u32 val)
{
	writel(val, chip->cport + offset);
}

inline static void
snd_nm256_write_buffer(nm256_t *chip, void *src, int offset, int size)
{
	offset -= chip->buffer_start;
#ifdef SNDRV_CONFIG_DEBUG
	if (offset < 0 || offset >= chip->buffer_size) {
		printk("nm256: write_buffer invalid offset = %d size = %d\n", offset, size);
		return;
	}
#endif
	memcpy_toio(chip->buffer + offset, src, size);
}

/*
 * coefficient handlers -- what a magic!
 */

static u16
snd_nm256_get_start_offset(int which)
{
	u16 offset = 0;
	while (which-- > 0)
		offset += coefficient_sizes[which];
	return offset;
}

static void
snd_nm256_load_one_coefficient(nm256_t *chip, int stream, u32 port, int which)
{
	u32 coeff_buf = chip->coeff_buf[stream];
	u16 offset = snd_nm256_get_start_offset(which);
	u16 size = coefficient_sizes[which];

	snd_nm256_write_buffer(chip, coefficients + offset, coeff_buf, size);
	snd_nm256_writel(chip, port, coeff_buf);
	/* ???  Record seems to behave differently than playback.  */
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		size--;
	snd_nm256_writel(chip, port + 4, coeff_buf + size);
}

static void
snd_nm256_load_coefficient(nm256_t *chip, int stream, int number)
{
	/* The enable register for the specified engine.  */
	u32 poffset = (stream == SNDRV_PCM_STREAM_CAPTURE ? NM_RECORD_ENABLE_REG : SNDRV_PCM_STREAM_PLAYBACK_ENABLE_REG);
	u32 addr = NM_COEFF_START_OFFSET;

	addr += (stream == SNDRV_PCM_STREAM_CAPTURE ? NM_RECORD_REG_OFFSET : SNDRV_PCM_STREAM_PLAYBACK_REG_OFFSET);

	if (snd_nm256_readb(chip, poffset) & 1) {
		snd_printd("NM256: Engine was enabled while loading coefficients!\n");
		return;
	}

	/* The recording engine uses coefficient values 8-15.  */
	number &= 7;
	if (stream == SNDRV_PCM_STREAM_CAPTURE)
		number += 8;

	if (! chip->use_cache) {
		snd_nm256_load_one_coefficient(chip, stream, addr, number);
		return;
	}
	if (! chip->coeffs_current) {
		snd_nm256_write_buffer(chip, coefficients, chip->all_coeff_buf,
				       NM_TOTAL_COEFF_COUNT * 4);
		chip->coeffs_current = 1;
	} else {
		u32 base = chip->all_coeff_buf;
		u32 offset = snd_nm256_get_start_offset(number);
		u32 end_offset = offset + coefficient_sizes[number];
		snd_nm256_writel(chip, addr, base + offset);
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			end_offset--;
		snd_nm256_writel(chip, addr + 4, base + end_offset);
	}
}


/* The actual rates supported by the card. */
static int samplerates[8] = {
	8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000,
};

/*
 * return the index of the target rate
 */
static int
snd_nm256_fixed_rate(int rate)
{
	int i;

	if (rate < 8000)
		return 0;
	if (rate > 48000)
		return 7;

	for (i = 0; i < 7; i++) {
		if (rate < (samplerates[i] + samplerates[i + 1]) / 2)
			break;
	}
	return i;
}

/*
 * set sample rate and format
 */
static void
snd_nm256_set_format(nm256_t *chip, nm256_stream_t *s, snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int rate_index = snd_nm256_fixed_rate(runtime->rate);
	unsigned char ratebits = (rate_index << 4) & NM_RATE_MASK;

	s->shift = 0;
	if (snd_pcm_format_width(runtime->format) == 16) {
		ratebits |= NM_RATE_BITS_16;
		s->shift++;
	}
	if (runtime->channels > 1) {
		ratebits |= NM_RATE_STEREO;
		s->shift++;
	}

	runtime->rate = samplerates[rate_index];

#if 0
	if (s->running)
		return;
#endif

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		snd_nm256_load_coefficient(chip, 0, rate_index); /* 0 = playback */
		snd_nm256_writeb(chip,
				 SNDRV_PCM_STREAM_PLAYBACK_REG_OFFSET + NM_RATE_REG_OFFSET,
				 ratebits);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		snd_nm256_load_coefficient(chip, 1, rate_index); /* 1 = record */
		snd_nm256_writeb(chip,
				 NM_RECORD_REG_OFFSET + NM_RATE_REG_OFFSET,
				 ratebits);
		break;
	}
}

/*
 * start / stop
 */

/* update the watermark (current period) */
static void snd_nm256_playback_mark(nm256_t *chip, nm256_stream_t *s)
{
	s->cur_frag++;
	s->cur_frag %= s->frags;
	snd_nm256_writel(chip, NM_PBUFFER_WMARK,
			 s->buf + s->cur_frag * s->period_size);
}

static void
snd_nm256_playback_start(nm256_t *chip, nm256_stream_t *s, snd_pcm_substream_t *substream)
{
	/* program buffer pointers */
	snd_nm256_writel(chip, NM_PBUFFER_START, s->buf);
	snd_nm256_writel(chip, NM_PBUFFER_END, s->buf + s->dma_size - (1 << s->shift));
	snd_nm256_writel(chip, NM_PBUFFER_CURRP, s->buf);
	snd_nm256_playback_mark(chip, s);

	/* Enable playback engine and interrupts. */
	snd_nm256_writeb(chip, SNDRV_PCM_STREAM_PLAYBACK_ENABLE_REG,
			 SNDRV_PCM_STREAM_PLAYBACK_ENABLE_FLAG | SNDRV_PCM_STREAM_PLAYBACK_FREERUN);
	/* Enable both channels. */
	snd_nm256_writew(chip, NM_AUDIO_MUTE_REG, 0x0);
}

/* update the watermark (current period) */
static void snd_nm256_capture_mark(nm256_t *chip, nm256_stream_t *s)
{
	s->cur_frag++;
	s->cur_frag %= s->frags;
	snd_nm256_writel(chip, NM_RBUFFER_WMARK,
			 s->buf + s->cur_frag * s->period_size);
}

static void
snd_nm256_capture_start(nm256_t *chip, nm256_stream_t *s, snd_pcm_substream_t *substream)
{
	/* program buffer pointers */
	snd_nm256_writel(chip, NM_RBUFFER_START, s->buf);
	snd_nm256_writel(chip, NM_RBUFFER_END, s->buf + s->dma_size);
	snd_nm256_writel(chip, NM_RBUFFER_CURRP, s->buf);
	snd_nm256_capture_mark(chip, s);

	/* Enable playback engine and interrupts. */
	snd_nm256_writeb(chip, NM_RECORD_ENABLE_REG,
			 NM_RECORD_ENABLE_FLAG | NM_RECORD_FREERUN);
}

/* Stop the play engine. */
static void
snd_nm256_playback_stop(nm256_t *chip)
{
	/* Shut off sound from both channels. */
	snd_nm256_writew(chip, NM_AUDIO_MUTE_REG,
			 NM_AUDIO_MUTE_LEFT | NM_AUDIO_MUTE_RIGHT);
	/* Disable play engine. */
	snd_nm256_writeb(chip, SNDRV_PCM_STREAM_PLAYBACK_ENABLE_REG, 0);
}

static void
snd_nm256_capture_stop(nm256_t *chip)
{
	/* Disable recording engine. */
	snd_nm256_writeb(chip, NM_RECORD_ENABLE_REG, 0);
}

static int
snd_nm256_playback_trigger(snd_pcm_substream_t *substream, int cmd)
{
	nm256_t *chip = snd_pcm_substream_chip(substream);
	nm256_stream_t *s = (nm256_stream_t*)substream->runtime->private_data;
	unsigned long flags;
	int err = 0;
	snd_assert(s != NULL, return -ENXIO);

	spin_lock_irqsave(&chip->reg_lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (! s->running)
			snd_nm256_playback_start(chip, s, substream);
		s->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (s->running)
			snd_nm256_playback_stop(chip);
		s->running = 0;
		break;
	default:
		err = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return err;
}

static int
snd_nm256_capture_trigger(snd_pcm_substream_t *substream, int cmd)
{
	nm256_t *chip = snd_pcm_substream_chip(substream);
	nm256_stream_t *s = (nm256_stream_t*)substream->runtime->private_data;
	unsigned long flags;
	int err = 0;

	snd_assert(s != NULL, return -ENXIO);

	spin_lock_irqsave(&chip->reg_lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (! s->running)
			snd_nm256_capture_start(chip, s, substream);
		s->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (s->running)
			snd_nm256_capture_stop(chip);
		s->running = 0;
		break;
	default:
		err = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return err;
}


/*
 * calculate period size
 */
static int snd_nm256_set_period_size(nm256_t *chip, nm256_stream_t *s,
				   snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int align;

	s->dma_size = frames_to_bytes(runtime, runtime->buffer_size);
	if (s->dma_size > s->bufsize)
		s->dma_size = s->bufsize;
	s->period_size = frames_to_bytes(runtime, runtime->period_size);
	if (s->period_size > s->dma_size / 2)
		s->period_size = s->dma_size / 2;
	if ((align = s->dma_size % s->period_size) != 0)
		s->period_size -= align;
	runtime->buffer_size = bytes_to_frames(runtime, s->dma_size);
	runtime->period_size = bytes_to_frames(runtime, s->period_size);
	runtime->periods = runtime->buffer_size / runtime->period_size;
	snd_assert(s->frags > 0, return -EINVAL);
	return 0;
}

/*
 * prepare playback/capture channel
 */
static int snd_nm256_pcm_prepare(snd_pcm_substream_t *substream)
{
	nm256_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	nm256_stream_t *s = (nm256_stream_t*)runtime->private_data;
	unsigned long flags;
	int err;

	snd_assert(s != NULL, return -ENXIO);
	if ((err = snd_nm256_set_period_size(chip, s, substream)) < 0)
		return err;

	spin_lock_irqsave(&chip->reg_lock, flags);
	s->running = 0;
	s->cur_frag = 0;
	snd_nm256_set_format(chip, s, substream);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	return 0;
}


/*
 * get the current pointer
 */

static snd_pcm_uframes_t
snd_nm256_playback_pointer(snd_pcm_substream_t * substream)
{
	nm256_t *chip = snd_pcm_substream_chip(substream);
	nm256_stream_t *s = (nm256_stream_t*)substream->runtime->private_data;
	unsigned long curp;

	snd_assert(s != NULL, return 0);
	curp = snd_nm256_readl(chip, NM_PBUFFER_CURRP) - (unsigned long)s->buf;
	curp %= s->dma_size;
	return bytes_to_frames(substream->runtime, curp);
}

static snd_pcm_uframes_t
snd_nm256_capture_pointer(snd_pcm_substream_t * substream)
{
	nm256_t *chip = snd_pcm_substream_chip(substream);
	nm256_stream_t *s = (nm256_stream_t*)substream->runtime->private_data;
	unsigned long curp;

	snd_assert(s != NULL, return 0);
	curp = snd_nm256_readl(chip, NM_RBUFFER_CURRP) - (unsigned long)s->buf;
	curp %= s->dma_size;	
	return bytes_to_frames(substream->runtime, curp);
}

static int
snd_nm256_playback_silence(snd_pcm_substream_t *substream,
			   int channel, /* not used (interleaved data) */
			   snd_pcm_uframes_t pos,
			   snd_pcm_uframes_t count)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	nm256_stream_t *s = (nm256_stream_t*)runtime->private_data;
	count = frames_to_bytes(runtime, count);
	pos = frames_to_bytes(runtime, pos);
	memset_io(s->bufptr + pos, 0, count);
	return 0;
}

static int
snd_nm256_playback_copy(snd_pcm_substream_t *substream,
			int channel, /* not used (interleaved data) */
			snd_pcm_uframes_t pos,
			void *src,
			snd_pcm_uframes_t count)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	nm256_stream_t *s = (nm256_stream_t*)runtime->private_data;
	count = frames_to_bytes(runtime, count);
	pos = frames_to_bytes(runtime, pos);
	if (copy_from_user_toio(s->bufptr + pos, src, count))
		return -EFAULT;
	return 0;
}

static int
snd_nm256_capture_copy(snd_pcm_substream_t *substream,
		       int channel, /* not used (interleaved data) */
		       snd_pcm_uframes_t pos,
		       void *dst,
		       snd_pcm_uframes_t count)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	nm256_stream_t *s = (nm256_stream_t*)runtime->private_data;
	count = frames_to_bytes(runtime, count);
	pos = frames_to_bytes(runtime, pos);
	if (copy_to_user_fromio(dst, s->bufptr + pos, count))
		return -EFAULT;
	return 0;
}


/*
 * update playback/capture watermarks
 */

/* spinlock held! */
static void
snd_nm256_playback_update(nm256_t *chip)
{
	nm256_stream_t *s;

	s = &chip->streams[SNDRV_PCM_STREAM_PLAYBACK];
	if (s->running && s->substream) {
		spin_unlock(&chip->reg_lock);
		snd_pcm_period_elapsed(s->substream);
		spin_lock(&chip->reg_lock);
		snd_nm256_playback_mark(chip, s);
	}
}

/* spinlock held! */
static void
snd_nm256_capture_update(nm256_t *chip)
{
	nm256_stream_t *s;

	s = &chip->streams[SNDRV_PCM_STREAM_CAPTURE];
	if (s->running && s->substream) {
		spin_unlock(&chip->reg_lock);
		snd_pcm_period_elapsed(s->substream);
		spin_lock(&chip->reg_lock);
		snd_nm256_capture_mark(chip, s);
	}
}

/*
 */

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_nm256_playback =
{
/*	info:		  */	(/*SNDRV_PCM_CHNINFO_MMAP|SNDRV_PCM_CHNINFO_MMAP_VALID|*/
				 SNDRV_PCM_INFO_INTERLEAVED),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	8000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	128 * 1024,
/*	period_bytes_min:  */	256,
/*	period_bytes_max:  */	128 * 1024,
        0,0,0
};

static snd_pcm_hardware_t snd_nm256_capture =
{
/*	info:		  */	(/*SNDRV_PCM_CHNINFO_MMAP|SNDRV_PCM_CHNINFO_MMAP_VALID|*/
				 SNDRV_PCM_INFO_INTERLEAVED),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	8000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	128 * 1024,
/*	period_bytes_min:  */	256,
/*	period_bytes_max:  */	128 * 1024,
        0,0,0
};
#else
static snd_pcm_hardware_t snd_nm256_playback =
{
	info:			(/*SNDRV_PCM_CHNINFO_MMAP|SNDRV_PCM_CHNINFO_MMAP_VALID|*/
				 SNDRV_PCM_INFO_INTERLEAVED),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_8000_48000,
	rate_min:		8000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	128 * 1024,
	period_bytes_min:	256,
	period_bytes_max:	128 * 1024,
};

static snd_pcm_hardware_t snd_nm256_capture =
{
	info:			(/*SNDRV_PCM_CHNINFO_MMAP|SNDRV_PCM_CHNINFO_MMAP_VALID|*/
				 SNDRV_PCM_INFO_INTERLEAVED),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_8000_48000,
	rate_min:		8000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	128 * 1024,
	period_bytes_min:	256,
	period_bytes_max:	128 * 1024,
};
#endif

/*
 * playback
 */
static int
snd_nm256_playback_open(snd_pcm_substream_t *substream)
{
	nm256_t *chip = snd_pcm_substream_chip(substream);
	nm256_stream_t *s;
	snd_pcm_runtime_t *runtime = substream->runtime;

	s = &chip->streams[SNDRV_PCM_STREAM_PLAYBACK];
	s->running = 0;

	runtime->hw = snd_nm256_playback;
	runtime->hw.period_bytes_max = s->bufsize;
	runtime->dma_area = (void *)s->bufptr;
	runtime->dma_bytes = s->bufsize;
	runtime->private_data = s;
	s->substream = substream;

	snd_pcm_set_sync(substream);

	return 0;
}

static int
snd_nm256_playback_close(snd_pcm_substream_t *substream)
{
	/* nm256_t *chip = snd_pcm_substream_chip(substream); */
	return 0;
}


/*
 */
static int
snd_nm256_capture_open(snd_pcm_substream_t *substream)
{
	nm256_t *chip = snd_pcm_substream_chip(substream);
	nm256_stream_t *s;
	snd_pcm_runtime_t *runtime = substream->runtime;

	s = &chip->streams[SNDRV_PCM_STREAM_CAPTURE];
	s->running = 0;

	runtime->hw = snd_nm256_capture;
	runtime->hw.period_bytes_max = s->bufsize;
	runtime->dma_area = (void*) s->bufptr;
	runtime->dma_bytes = s->bufsize;
	runtime->private_data = s;
	s->substream = substream;

	snd_pcm_set_sync(substream);

	return 0;
}

static int
snd_nm256_capture_close(snd_pcm_substream_t *substream)
{
	/* nm256_t *chip = snd_pcm_substream_chip(substream); */
	return 0;
}

/*
 * create a pcm instance
 */

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_nm256_playback_ops = {
	snd_nm256_playback_open,
	snd_nm256_playback_close,
	snd_pcm_lib_ioctl,0,0,
	snd_nm256_pcm_prepare,
	snd_nm256_playback_trigger,
	snd_nm256_playback_pointer,
	snd_nm256_playback_copy,
	snd_nm256_playback_silence,
};

static snd_pcm_ops_t snd_nm256_capture_ops = {
	snd_nm256_capture_open,
	snd_nm256_capture_close,
	snd_pcm_lib_ioctl,0,0,
	snd_nm256_pcm_prepare,
	snd_nm256_capture_trigger,
	snd_nm256_capture_pointer,
	snd_nm256_capture_copy,0
};
#else
static snd_pcm_ops_t snd_nm256_playback_ops = {
	open:		snd_nm256_playback_open,
	close:		snd_nm256_playback_close,
	ioctl:		snd_pcm_lib_ioctl,
	prepare:	snd_nm256_pcm_prepare,
	trigger:	snd_nm256_playback_trigger,
	pointer:	snd_nm256_playback_pointer,
	copy:		snd_nm256_playback_copy,
	silence:	snd_nm256_playback_silence,
};

static snd_pcm_ops_t snd_nm256_capture_ops = {
	open:		snd_nm256_capture_open,
	close:		snd_nm256_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	prepare:	snd_nm256_pcm_prepare,
	trigger:	snd_nm256_capture_trigger,
	pointer:	snd_nm256_capture_pointer,
	copy:		snd_nm256_capture_copy,
};
#endif

static int __init
snd_nm256_pcm(nm256_t *chip, int device)
{
	snd_pcm_t *pcm;
	int i, err;

	for (i = 0; i < 2; i++) {
		nm256_stream_t *s = &chip->streams[i];
		s->bufptr = chip->buffer +  s->buf - chip->buffer_start;
	}

	err = snd_pcm_new(chip->card, chip->card->driver, device,
			  1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_nm256_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_nm256_capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	chip->pcm = pcm;

	return 0;
}


/* 
 * Initialize the hardware. 
 */
static void
snd_nm256_init_chip(nm256_t *chip)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	/* Reset everything. */
	snd_nm256_writeb(chip, 0x0, 0x11);
	snd_nm256_writew(chip, 0x214, 0);
	/* stop sounds.. */
	//snd_nm256_playback_stop(chip);
	//snd_nm256_capture_stop(chip);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}


inline static void
snd_nm256_intr_check(nm256_t *chip)
{
	if (chip->badintrcount++ > 1000) {
		/*
		 * I'm not sure if the best thing is to stop the card from
		 * playing or just release the interrupt (after all, we're in
		 * a bad situation, so doing fancy stuff may not be such a good
		 * idea).
		 *
		 * I worry about the card engine continuing to play noise
		 * over and over, however--that could become a very
		 * obnoxious problem.  And we know that when this usually
		 * happens things are fairly safe, it just means the user's
		 * inserted a PCMCIA card and someone's spamming us with IRQ 9s.
		 */
		if (chip->streams[SNDRV_PCM_STREAM_PLAYBACK].running)
			snd_nm256_playback_stop(chip);
		if (chip->streams[SNDRV_PCM_STREAM_CAPTURE].running)
			snd_nm256_capture_stop(chip);
		chip->badintrcount = 0;
	}
}

/* 
 * Handle a potential interrupt for the device referred to by DEV_ID. 
 *
 * I don't like the cut-n-paste job here either between the two routines,
 * but there are sufficient differences between the two interrupt handlers
 * that parameterizing it isn't all that great either.  (Could use a macro,
 * I suppose...yucky bleah.)
 */

static void
snd_nm256_interrupt(int irq, void *dev_id, struct pt_regs *dummy)
{
	nm256_t *chip = snd_magic_cast(nm256_t, dev_id, return);
	u16 status;
	u8 cbyte;

	status = snd_nm256_readw(chip, NM_INT_REG);

	/* Not ours. */
	if (status == 0) {
		snd_nm256_intr_check(chip);
		return;
	}

	chip->badintrcount = 0;

	/* Rather boring; check for individual interrupts and process them. */

	spin_lock(&chip->reg_lock);
	if (status & SNDRV_PCM_STREAM_PLAYBACK_INT) {
		status &= ~SNDRV_PCM_STREAM_PLAYBACK_INT;
		NM_ACK_INT(chip, SNDRV_PCM_STREAM_PLAYBACK_INT);
		snd_nm256_playback_update(chip);
	}

	if (status & NM_RECORD_INT) {
		status &= ~NM_RECORD_INT;
		NM_ACK_INT(chip, NM_RECORD_INT);
		snd_nm256_capture_update(chip);
	}

	if (status & NM_MISC_INT_1) {
		status &= ~NM_MISC_INT_1;
		NM_ACK_INT(chip, NM_MISC_INT_1);
		snd_printd("NM256: Got misc interrupt #1\n");
		snd_nm256_writew(chip, NM_INT_REG, 0x8000);
		cbyte = snd_nm256_readb(chip, 0x400);
		snd_nm256_writeb(chip, 0x400, cbyte | 2);
	}

	if (status & NM_MISC_INT_2) {
		status &= ~NM_MISC_INT_2;
		NM_ACK_INT(chip, NM_MISC_INT_2);
		snd_printd("NM256: Got misc interrupt #2\n");
		cbyte = snd_nm256_readb(chip, 0x400);
		snd_nm256_writeb(chip, 0x400, cbyte & ~2);
	}

	/* Unknown interrupt. */
	if (status) {
		snd_printd("NM256: Fire in the hole! Unknown status 0x%x\n",
			   status);
		/* Pray. */
		NM_ACK_INT(chip, status);
	}

	spin_unlock(&chip->reg_lock);
}

/*
 * Handle a potential interrupt for the device referred to by DEV_ID.
 * This handler is for the 256ZX, and is very similar to the non-ZX
 * routine.
 */

static void
snd_nm256_interrupt_zx(int irq, void *dev_id, struct pt_regs *dummy)
{
	nm256_t *chip = snd_magic_cast(nm256_t, dev_id, return);
	u32 status;
	u8 cbyte;

	status = snd_nm256_readl(chip, NM_INT_REG);

	/* Not ours. */
	if (status == 0) {
		snd_nm256_intr_check(chip);
		return;
	}

	chip->badintrcount = 0;

	/* Rather boring; check for individual interrupts and process them. */

	spin_lock(&chip->reg_lock);
	if (status & NM2_PLAYBACK_INT) {
		status &= ~NM2_PLAYBACK_INT;
		NM2_ACK_INT(chip, NM2_PLAYBACK_INT);
		snd_nm256_playback_update(chip);
	}

	if (status & NM2_RECORD_INT) {
		status &= ~NM2_RECORD_INT;
		NM2_ACK_INT(chip, NM2_RECORD_INT);
		snd_nm256_capture_update(chip);
	}

	if (status & NM2_MISC_INT_1) {
		status &= ~NM2_MISC_INT_1;
		NM2_ACK_INT(chip, NM2_MISC_INT_1);
		snd_printd("NM256: Got misc interrupt #1\n");
		cbyte = snd_nm256_readb(chip, 0x400);
		snd_nm256_writeb(chip, 0x400, cbyte | 2);
	}

	if (status & NM2_MISC_INT_2) {
		status &= ~NM2_MISC_INT_2;
		NM2_ACK_INT(chip, NM2_MISC_INT_2);
		snd_printd("NM256: Got misc interrupt #2\n");
		cbyte = snd_nm256_readb(chip, 0x400);
		snd_nm256_writeb(chip, 0x400, cbyte & ~2);
	}

	/* Unknown interrupt. */
	if (status) {
		snd_printd("NM256: Fire in the hole! Unknown status 0x%x\n",
			   status);
		/* Pray. */
		NM2_ACK_INT(chip, status);
	}

	spin_unlock(&chip->reg_lock);
}

/*
 * AC97 interface
 */

/*
 * Waits for the mixer to become ready to be written; returns a zero value
 * if it timed out.
 */
/* spinlock held! */
static int
snd_nm256_ac97_ready(nm256_t *chip)
{
	int timeout = 10;
	u32 testaddr;
	u16 testb;

	testaddr = chip->mixer_status_offset;
	testb = chip->mixer_status_mask;

	/* 
	 * Loop around waiting for the mixer to become ready. 
	 */
	while (timeout-- > 0) {
		if ((snd_nm256_readw(chip, testaddr) & testb) == 0)
			return 1;
		udelay(100);
	}
	return 0;
}

/*
 */
static unsigned short
snd_nm256_ac97_read(ac97_t *ac97, unsigned short reg)
{
	nm256_t *chip = snd_magic_cast(nm256_t, ac97->private_data, return -ENXIO);
	unsigned long flags;
	int res;

	if (reg >= 128)
		return 0;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (! snd_nm256_ac97_ready(chip)) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return 0;
	}
	res = snd_nm256_readw(chip, chip->mixer_base + reg);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	/* Magic delay.  Bleah yucky.  */
	udelay(1000);
	return res;
}

/* 
 */
static void
snd_nm256_ac97_write(ac97_t *ac97,
		     unsigned short reg, unsigned short val)
{
	nm256_t *chip = snd_magic_cast(nm256_t, ac97->private_data, return);
	unsigned long flags;
	int tries = 2;
	u32 base;

	base = chip->mixer_base;

	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_nm256_ac97_ready(chip);

	/* Wait for the write to take, too. */
	while (tries-- > 0) {
		snd_nm256_writew(chip, base + reg, val);
		udelay(1000);  /* a little delay here seems better.. */
		if (snd_nm256_ac97_ready(chip))
			goto __ok;
	}
	snd_printd("nm256: ac97 codec not ready..\n");
__ok:
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/* initialize the ac97 into a known state */
static void
snd_nm256_ac97_reset(ac97_t *ac97)
{
	nm256_t *chip = snd_magic_cast(nm256_t, ac97->private_data, return);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	/* Reset the mixer.  'Tis magic!  */
	snd_nm256_writeb(chip, 0x6c0, 1);
	snd_nm256_writeb(chip, 0x6cc, 0x87);
	snd_nm256_writeb(chip, 0x6cc, 0x80);
	snd_nm256_writeb(chip, 0x6cc, 0x0);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/* create an ac97 mixer interface */
static int __init
snd_nm256_mixer(nm256_t *chip)
{
	ac97_t ac97;
	int err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.init = snd_nm256_ac97_reset;
	ac97.write = snd_nm256_ac97_write;
	ac97.read = snd_nm256_ac97_read;
	ac97.private_data = chip;
	if ((err = snd_ac97_mixer(chip->card, &ac97, &chip->ac97)) < 0)
		return err;

	return 0;
}

/* 
 * See if the signature left by the NM256 BIOS is intact; if so, we use
 * the associated address as the end of our audio buffer in the video
 * RAM.
 */

static int __init
snd_nm256_peek_for_sig(nm256_t *chip)
{
	/* The signature is located 1K below the end of video RAM.  */
	unsigned long temp;
	/* Default buffer end is 5120 bytes below the top of RAM.  */
	unsigned long pointer_found = chip->buffer_end - 0x1400;
	u32 sig;

	temp = (unsigned long) ioremap_nocache(chip->buffer_addr + chip->buffer_end - 0x400, 16);
	if (temp == 0) {
		snd_printk("Unable to scan for card signature in video RAM\n");
		return -EBUSY;
	}

	sig = readl(temp);
	if ((sig & NM_SIG_MASK) == NM_SIGNATURE) {
		u32 pointer = readl(temp + 4);

		/*
		 * If it's obviously invalid, don't use it
		 */
		if (pointer == 0xffffffff ||
		    pointer < chip->buffer_size ||
		    pointer > chip->buffer_end)
			snd_printk("invalid signature found: 0x%x\n", pointer);
		else {
			pointer_found = pointer;
			snd_printd("Found card signature in video RAM: 0x%x\n", pointer);
		}
	}

	iounmap((void *)temp);
	chip->buffer_end = pointer_found;

	return 0;
}

#ifdef CONFIG_PM
/*
 * APM event handler, so the card is properly reinitialized after a power
 * event.
 */
#ifdef PCI_NEW_SUSPEND
static int snd_nm256_suspend(struct pci_dev *dev, u32 state)
#else
static void snd_nm256_suspend(struct pci_dev *dev)
#endif
{
#ifdef PCI_NEW_SUSPEND
	nm256_t *chip = snd_magic_cast(nm256_t, PCI_GET_DRIVER_DATA(dev), return -ENXIO);
#else
	nm256_t *chip = snd_magic_cast(nm256_t, PCI_GET_DRIVER_DATA(dev), return);
#endif
	unsigned long flags;

	if (! chip)
#ifdef PCI_NEW_SUSPEND
		return -ENXIO;
#else
		return;
#endif

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->streams[SNDRV_PCM_STREAM_PLAYBACK].running)
		snd_nm256_playback_stop(chip);
	if (chip->streams[SNDRV_PCM_STREAM_CAPTURE].running)
		snd_nm256_capture_stop(chip);
	chip->coeffs_current = 0;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
#ifdef PCI_NEW_SUSPEND
	return 0;
#endif
}

#ifdef PCI_NEW_SUSPEND
static int snd_nm256_resume(struct pci_dev *dev)
#else
static void snd_nm256_resume(struct pci_dev *dev)
#endif
{
#ifdef PCI_NEW_SUSPEND
	nm256_t *chip = snd_magic_cast(nm256_t, PCI_GET_DRIVER_DATA(dev), return -ENXIO);
#else
	nm256_t *chip = snd_magic_cast(nm256_t, PCI_GET_DRIVER_DATA(dev), return);
#endif
	nm256_stream_t *s;
	unsigned long flags;

	if (! chip)
#ifdef PCI_NEW_SUSPEND
		return -ENXIO;
#else
		return;
#endif

	/* Perform a full reset on the hardware */
	snd_nm256_init_chip(chip);

	/* restore ac97 */
	snd_ac97_resume(chip->ac97);

	/* start dma */
	spin_lock_irqsave(&chip->reg_lock, flags);
	s = &chip->streams[SNDRV_PCM_STREAM_PLAYBACK];
	if (s->running && s->substream) {
		//snd_nm256_set_format(chip, s, s->substream);
		snd_nm256_playback_start(chip, s, s->substream);
	}
	s = &chip->streams[SNDRV_PCM_STREAM_CAPTURE];
	if (s->running && s->substream) {
		//snd_nm256_set_format(chip, s, s->substream);
		snd_nm256_capture_start(chip, s, s->substream);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
#ifdef PCI_NEW_SUSPEND
	return 0;
#endif
}
#endif

static int snd_nm256_free(nm256_t *chip)
{
	if (chip->streams[SNDRV_PCM_STREAM_PLAYBACK].running)
		snd_nm256_playback_stop(chip);
	if (chip->streams[SNDRV_PCM_STREAM_CAPTURE].running)
		snd_nm256_capture_stop(chip);

	synchronize_irq();

	if (chip->cport)
		iounmap((void *) chip->cport);
	if (chip->buffer)
		iounmap((void *) chip->buffer);
	if (chip->res_cport)
		release_resource(chip->res_cport);
	if (chip->res_buffer)
		release_resource(chip->res_buffer);
	if (chip->irq >= 0)
		free_irq(chip->irq, (void*)chip);

	snd_magic_kfree(chip);
	return 0;
}

static int snd_nm256_dev_free(snd_device_t *device)
{
	nm256_t *chip = snd_magic_cast(nm256_t, device->device_data, return -ENXIO);
	return snd_nm256_free(chip);
}

static int __init
snd_nm256_create(snd_card_t *card, struct pci_dev *pci,
		 int play_bufsize, int capt_bufsize,
		 int force_load,
		 u32 buffertop,
		 int usecache,
		 nm256_t **chip_ret)
{
	nm256_t *chip;
	int err, pval;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_nm256_dev_free,0,0
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_nm256_dev_free,
	};
#endif
	*chip_ret = NULL;

	chip = snd_magic_kcalloc(nm256_t, 0, GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->card = card;
	chip->pci = pci;
	chip->use_cache = usecache;
	spin_lock_init(&chip->reg_lock);
	chip->irq = -1;

	chip->streams[SNDRV_PCM_STREAM_PLAYBACK].bufsize = play_bufsize;
	chip->streams[SNDRV_PCM_STREAM_CAPTURE].bufsize = capt_bufsize;

	/* 
	 * The NM256 has two memory ports.  The first port is nothing
	 * more than a chunk of video RAM, which is used as the I/O ring
	 * buffer.  The second port has the actual juicy stuff (like the
	 * mixer and the playback engine control registers).
	 */

	chip->buffer_addr = pci_resource_start(pci, 0);
	chip->cport_addr = pci_resource_start(pci, 1);

	/* Init the memory port info.  */
	/* remap control port (#2) */
	chip->res_cport = request_mem_region(chip->cport_addr, NM_PORT2_SIZE,
					     card->driver);
	if (chip->res_cport == NULL) {
		snd_printk("memory region 0x%lx (size 0x%x) busy\n",
			   chip->cport_addr, NM_PORT2_SIZE);
		err = -EBUSY;
		goto __error;
	}
	chip->cport = (unsigned long) ioremap_nocache(chip->cport_addr, NM_PORT2_SIZE);
	if (chip->cport == 0) {
		snd_printk("unable to map control port %lx\n", chip->cport_addr);
		err = -ENOMEM;
		goto __error;
	}

	if (!strcmp(card->driver, "NM256AV")) {
		/* Ok, try to see if this is a non-AC97 version of the hardware. */
		pval = snd_nm256_readw(chip, NM_MIXER_PRESENCE);
		if ((pval & NM_PRESENCE_MASK) != NM_PRESENCE_VALUE) {
			if (! force_load) {
				snd_printk("nm256: no ac97 is found!\n");
				snd_printk("  force the driver to load by passing in the module parameter\n");
				snd_printk("    snd_force_ac97=1\n");
				snd_printk("  or try  sb16 or cs423x drivers instead.\n");
				err = -ENXIO;
				goto __error;
			}
		}
		chip->buffer_end = 2560 * 1024;
		chip->interrupt = snd_nm256_interrupt;
		chip->mixer_status_offset = NM_MIXER_STATUS_OFFSET;
		chip->mixer_status_mask = NM_MIXER_READY_MASK;
	} else {
		/* Not sure if there is any relevant detect for the ZX or not.  */
		if (snd_nm256_readb(chip, 0xa0b) != 0)
			chip->buffer_end = 6144 * 1024;
		else
			chip->buffer_end = 4096 * 1024;

		chip->interrupt = snd_nm256_interrupt_zx;
		chip->mixer_status_offset = NM2_MIXER_STATUS_OFFSET;
		chip->mixer_status_mask = NM2_MIXER_READY_MASK;
	}
	
	chip->buffer_size = chip->streams[SNDRV_PCM_STREAM_PLAYBACK].bufsize + chip->streams[SNDRV_PCM_STREAM_CAPTURE].bufsize;
	if (chip->use_cache)
		chip->buffer_size += NM_TOTAL_COEFF_COUNT * 4;
	else
		chip->buffer_size += NM_MAX_COEFFICIENT * 2;

	if (buffertop >= chip->buffer_size && buffertop < chip->buffer_end)
		chip->buffer_end = buffertop;
	else {
		/* get buffer end pointer from signature */
		if ((err = snd_nm256_peek_for_sig(chip)) < 0)
			goto __error;
	}

	chip->buffer_start = chip->buffer_end - chip->buffer_size;
	chip->buffer_addr += chip->buffer_start;

	snd_printd("NM256: Mapping port 1 from 0x%x - 0x%x\n",
		   chip->buffer_start, chip->buffer_end);

	chip->res_buffer = request_mem_region(chip->buffer_addr,
					      chip->buffer_size,
					      card->driver);
	if (chip->res_buffer == NULL) {
		snd_printk("nm256: buffer 0x%lx (size 0x%x) busy\n",
			   chip->buffer_addr, chip->buffer_size);
		err = -EBUSY;
		goto __error;
	}
	chip->buffer = (unsigned long) ioremap_nocache(chip->buffer_addr, chip->buffer_size);
	if (chip->buffer == 0) {
		err = -ENOMEM;
		snd_printk("unable to map ring buffer at %lx\n", chip->buffer_addr);
		goto __error;
	}

	/* set offsets */
	chip->streams[SNDRV_PCM_STREAM_PLAYBACK].buf = chip->buffer_start;
	chip->streams[SNDRV_PCM_STREAM_CAPTURE].buf = chip->streams[SNDRV_PCM_STREAM_PLAYBACK].buf + chip->streams[SNDRV_PCM_STREAM_PLAYBACK].bufsize;
	if (chip->use_cache) {
		chip->all_coeff_buf = chip->streams[SNDRV_PCM_STREAM_CAPTURE].buf + chip->streams[SNDRV_PCM_STREAM_CAPTURE].bufsize;
	} else {
		chip->coeff_buf[SNDRV_PCM_STREAM_PLAYBACK] = chip->streams[SNDRV_PCM_STREAM_CAPTURE].buf + chip->streams[SNDRV_PCM_STREAM_CAPTURE].bufsize;
		chip->coeff_buf[SNDRV_PCM_STREAM_CAPTURE] = chip->coeff_buf[SNDRV_PCM_STREAM_PLAYBACK] + NM_MAX_COEFFICIENT;
	}

	/* acquire interrupt */
	if (request_irq(pci->irq, chip->interrupt, SA_INTERRUPT|SA_SHIRQ,
			card->driver, (void*)chip)) {
		err = -EBUSY;
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		goto __error;
	}

	/* Fixed setting. */
	chip->mixer_base = NM_MIXER_OFFSET;

	chip->coeffs_current = 0;

	snd_nm256_init_chip(chip);

	if ((err = snd_nm256_pcm(chip, 0)) < 0)
		goto __error;
	
	if ((err = snd_nm256_mixer(chip) < 0))
		goto __error;

	// pci_set_master(pci); /* needed? */
	
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0)
		goto __error;

	*chip_ret = chip;
	return 0;

__error:
	snd_nm256_free(chip);
	return err;
}


static int __init snd_nm256_probe(struct pci_dev *pci,
				  const struct pci_device_id *id)
{
	static int dev = 0;
	snd_card_t *card;
	nm256_t *chip;
	int err;
	int buffer_top;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev]) {
			dev++;
			return -ENOENT;
		}
		break;
	}
	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	switch (pci->device) {
	case PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO:
		strcpy(card->driver, "NM256AV");
		break;
	case PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO:
		strcpy(card->driver, "NM256ZX");
		break;
	default:
		snd_printk("invalid device id 0x%x\n", pci->device);
		snd_card_free(card);
		return -EINVAL;
	}

	if (snd_vaio_hack[dev])
		buffer_top = 0x25a800;	/* this avoids conflicts with XFree86 server */
	else
		buffer_top = snd_buffer_top[dev];

	if (snd_playback_bufsize[dev] < 4)
		snd_playback_bufsize[dev] = 4;
	if (snd_playback_bufsize[dev] > 128)
		snd_playback_bufsize[dev] = 128;
	if (snd_capture_bufsize[dev] < 4)
		snd_capture_bufsize[dev] = 4;
	if (snd_capture_bufsize[dev] > 128)
		snd_capture_bufsize[dev] = 128;
	if ((err = snd_nm256_create(card, pci,
				    snd_playback_bufsize[dev] * 1024, /* in bytes */
				    snd_capture_bufsize[dev] * 1024,  /* in bytes */
				    snd_force_ac97[dev],
				    snd_buffer_top[dev],
				    snd_use_cache[dev],
				    &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	sprintf(card->shortname, "NeoMagic %s", card->driver);
	sprintf(card->longname, "%s at 0x%lx & 0x%lx, irq %d",
		card->shortname,
		chip->buffer_addr, chip->cport_addr, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	PCI_SET_DRIVER_DATA(pci, chip);
	dev++;
	return 0;
}

static void __exit snd_nm256_remove(struct pci_dev *pci)
{
	nm256_t *chip = snd_magic_cast(nm256_t, PCI_GET_DRIVER_DATA(pci), return);
	if (chip)
		snd_card_free(chip->card);
	PCI_SET_DRIVER_DATA(pci, NULL);
}

#ifdef TARGET_OS2
static struct pci_driver driver = {
	0,0,"NeoMagic 256",
	snd_nm256_ids,
	snd_nm256_probe,
	snd_nm256_remove,
#ifdef CONFIG_PM
	snd_nm256_suspend,
	snd_nm256_resume,
#else
        0,0
#endif
};
#else
static struct pci_driver driver = {
	name: "NeoMagic 256",
	id_table: snd_nm256_ids,
	probe: snd_nm256_probe,
	remove: snd_nm256_remove,
#ifdef CONFIG_PM
	suspend: snd_nm256_suspend,
	resume: snd_nm256_resume,
#endif
};
#endif

static int __init alsa_card_nm256_init(void)
{
	int err;
	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		snd_printk("NeoMagic 256 audio soundchip not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_nm256_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_nm256_init)
module_exit(alsa_card_nm256_exit)

#ifndef MODULE

/* format is: snd-card-nm256=snd_enable,snd_index,snd_id,
			     snd_playback_bufsize,snd_capture_bufsize,
			     snd_force_ac97,snd_buffer_top,snd_use_cache */

static int __init alsa_card_nm256_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,&snd_playback_bufsize[nr_dev]) == 2 &&
	       get_option(&str,&snd_capture_bufsize[nr_dev]) == 2 &&
	       get_option(&str,&snd_force_ac97[nr_dev]) == 2 &&
	       get_option(&str,&snd_buffer_top[nr_dev]) == 2 &&
	       get_option(&str,&snd_use_cache[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-nm256=", alsa_card_nm256_setup);

#endif /* ifndef MODULE */
