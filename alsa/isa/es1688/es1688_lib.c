/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for control of ESS ES1688/688/488 chip
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
#include <sound/es1688.h>

#define chip_t es1688_t

static int snd_es1688_dsp_command(es1688_t *chip, unsigned char val)
{
	int i;

	for (i = 10000; i; i--)
		if ((inb(ES1688P(chip, STATUS)) & 0x80) == 0) {
			outb(val, ES1688P(chip, COMMAND));
			return 1;
		}
#ifdef CONFIG_SND_DEBUG
	printk("snd_es1688_dsp_command: timeout (0x%x)\n", val);
#endif
	return 0;
}

static int snd_es1688_dsp_get_byte(es1688_t *chip)
{
	int i;

	for (i = 1000; i; i--)
		if (inb(ES1688P(chip, DATA_AVAIL)) & 0x80)
			return inb(ES1688P(chip, READ));
	snd_printd("es1688 get byte failed: 0x%lx = 0x%x!!!\n", ES1688P(chip, DATA_AVAIL), inb(ES1688P(chip, DATA_AVAIL)));
	return -ENODEV;
}

static int snd_es1688_write(es1688_t *chip,
			    unsigned char reg, unsigned char data)
{
	if (!snd_es1688_dsp_command(chip, reg))
		return 0;
	return snd_es1688_dsp_command(chip, data);
}

int snd_es1688_read(es1688_t *chip, unsigned char reg)
{
	/* Read a byte from an extended mode register of ES1688 */
	if (!snd_es1688_dsp_command(chip, 0xc0))
		return -1;
	if (!snd_es1688_dsp_command(chip, reg))
		return -1;
	return snd_es1688_dsp_get_byte(chip);
}

void snd_es1688_mixer_write(es1688_t *chip,
			    unsigned char reg, unsigned char data)
{
	outb(reg, ES1688P(chip, MIXER_ADDR));
	udelay(10);
	outb(data, ES1688P(chip, MIXER_DATA));
	udelay(10);
}

unsigned char snd_es1688_mixer_read(es1688_t *chip, unsigned char reg)
{
	unsigned char result;

	outb(reg, ES1688P(chip, MIXER_ADDR));
	udelay(10);
	result = inb(ES1688P(chip, MIXER_DATA));
	udelay(10);
	return result;
}

static int snd_es1688_reset(es1688_t *chip)
{
	int i;

	outb(3, ES1688P(chip, RESET));		/* valid only for ESS chips, SB -> 1 */
	udelay(10);
	outb(0, ES1688P(chip, RESET));
	udelay(30);
	for (i = 0; i < 1000 && !(inb(ES1688P(chip, DATA_AVAIL)) & 0x80); i++);
	if (inb(ES1688P(chip, READ)) != 0xaa) {
		snd_printd("ess_reset at 0x%lx: failed!!!\n", chip->port);
		return -ENODEV;
	}
	snd_es1688_dsp_command(chip, 0xc6);	/* enable extended mode */
	return 0;
}

static int snd_es1688_probe(es1688_t *chip)
{
	unsigned long flags;
	unsigned short major, minor, hw;
	int i;

	/*
	 *  initialization sequence
	 */

	spin_lock_irqsave(&chip->reg_lock, flags);	/* Some ESS1688 cards need this */
	inb(ES1688P(chip, ENABLE1));	/* ENABLE1 */
	inb(ES1688P(chip, ENABLE1));	/* ENABLE1 */
	inb(ES1688P(chip, ENABLE1));	/* ENABLE1 */
	inb(ES1688P(chip, ENABLE2));	/* ENABLE2 */
	inb(ES1688P(chip, ENABLE1));	/* ENABLE1 */
	inb(ES1688P(chip, ENABLE2));	/* ENABLE2 */
	inb(ES1688P(chip, ENABLE1));	/* ENABLE1 */
	inb(ES1688P(chip, ENABLE1));	/* ENABLE1 */
	inb(ES1688P(chip, ENABLE2));	/* ENABLE2 */
	inb(ES1688P(chip, ENABLE1));	/* ENABLE1 */
	inb(ES1688P(chip, ENABLE0));	/* ENABLE0 */

	if (snd_es1688_reset(chip) < 0) {
		snd_printdd("ESS: [0x%lx] reset failed... 0x%x\n", chip->port, inb(ES1688P(chip, READ)));
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return -ENODEV;
	}
	snd_es1688_dsp_command(chip, 0xe7);	/* return identification */

	for (i = 1000, major = minor = 0; i; i--) {
		if (inb(ES1688P(chip, DATA_AVAIL)) & 0x80) {
			if (major == 0) {
				major = inb(ES1688P(chip, READ));
			} else {
				minor = inb(ES1688P(chip, READ));
			}
		}
	}

	spin_unlock_irqrestore(&chip->reg_lock, flags);

	snd_printdd("ESS: [0x%lx] found.. major = 0x%x, minor = 0x%x\n", chip->port, major, minor);

	chip->version = (major << 8) | minor;
	if (!chip->version)
		return -ENODEV;	/* probably SB */

	hw = ES1688_HW_AUTO;
	switch (chip->version & 0xfff0) {
	case 0x4880:
		snd_printk("[0x%lx] ESS: AudioDrive ES488 detected, but driver is in another place\n", chip->port);
		return -ENODEV;
	case 0x6880:
		hw = (chip->version & 0x0f) >= 8 ? ES1688_HW_1688 : ES1688_HW_688;
		break;
	default:
		snd_printk("[0x%lx] ESS: unknown AudioDrive chip with version 0x%x (Jazz16 soundcard?)\n", chip->port, chip->version);
		return -ENODEV;
	}

	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1688_write(chip, 0xb1, 0x10);	/* disable IRQ */
	snd_es1688_write(chip, 0xb2, 0x00);	/* disable DMA */
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	/* enable joystick, but disable OPL3 */
	spin_lock_irqsave(&chip->mixer_lock, flags);
	snd_es1688_mixer_write(chip, 0x40, 0x01);
	spin_unlock_irqrestore(&chip->mixer_lock, flags);

	return 0;
}

static int snd_es1688_init(es1688_t * chip, int enable)
{
	static int irqs[16] = {-1, -1, 0, -1, -1, 1, -1, 2, -1, 0, 3, -1, -1, -1, -1, -1};
	unsigned long flags;
	int cfg, irq_bits, dma, dma_bits, tmp, tmp1;

	/* ok.. setup MPU-401 port and joystick and OPL3 */
	cfg = 0x01;		/* enable joystick, but disable OPL3 */
	if (enable && chip->mpu_port >= 0x300 && chip->mpu_irq > 0 && chip->hardware != ES1688_HW_688) {
		tmp = (chip->mpu_port & 0x0f0) >> 4;
		if (tmp <= 3) {
			switch (chip->mpu_irq) {
			case 9:
				tmp1 = 4;
				break;
			case 5:
				tmp1 = 5;
				break;
			case 7:
				tmp1 = 6;
				break;
			case 10:
				tmp1 = 7;
				break;
			default:
				tmp1 = 0;
			}
			if (tmp1) {
				cfg |= (tmp << 3) | (tmp1 << 5);
			}
		}
	}
#if 0
	snd_printk("mpu cfg = 0x%x\n", cfg);
#endif
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1688_mixer_write(chip, 0x40, cfg);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	/* --- */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1688_read(chip, 0xb1);
	snd_es1688_read(chip, 0xb2);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (enable) {
		cfg = 0xf0;	/* enable only DMA counter interrupt */
		irq_bits = irqs[chip->irq & 0x0f];
		if (irq_bits < 0) {
			snd_printk("[0x%lx] ESS: bad IRQ %d for ES1688 chip!!\n", chip->port, chip->irq);
#if 0
			irq_bits = 0;
			cfg = 0x10;
#endif
			return -EINVAL;
		}
		spin_lock_irqsave(&chip->reg_lock, flags);
		snd_es1688_write(chip, 0xb1, cfg | (irq_bits << 2));
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		cfg = 0xf0;	/* extended mode DMA enable */
		dma = chip->dma8;
		if (dma > 3 || dma == 2) {
			snd_printk("[0x%lx] ESS: bad DMA channel %d for ES1688 chip!!\n", chip->port, dma);
#if 0
			dma_bits = 0;
			cfg = 0x00;	/* disable all DMA */
#endif
			return -EINVAL;
		} else {
			dma_bits = dma;
			if (dma != 3)
				dma_bits++;
		}
		spin_lock_irqsave(&chip->reg_lock, flags);
		snd_es1688_write(chip, 0xb2, cfg | (dma_bits << 2));
		spin_unlock_irqrestore(&chip->reg_lock, flags);
	} else {
		spin_lock_irqsave(&chip->reg_lock, flags);
		snd_es1688_write(chip, 0xb1, 0x10);	/* disable IRQ */
		snd_es1688_write(chip, 0xb2, 0x00);	/* disable DMA */
		spin_unlock_irqrestore(&chip->reg_lock, flags);
	}
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1688_read(chip, 0xb1);
	snd_es1688_read(chip, 0xb2);
	snd_es1688_reset(chip);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

/*

 */

#ifdef TARGET_OS2
static ratnum_t clocks[2] = {
	{
		795444,
		1,
		128,
		1,
	},
	{
		397722,
		1,
		128,
		1,
	}
};

static snd_pcm_hw_constraint_ratnums_t hw_constraints_clocks  = {
	2,
	clocks,
};
#else
static ratnum_t clocks[2] = {
	{
		num: 795444,
		den_min: 1,
		den_max: 128,
		den_step: 1,
	},
	{
		num: 397722,
		den_min: 1,
		den_max: 128,
		den_step: 1,
	}
};

static snd_pcm_hw_constraint_ratnums_t hw_constraints_clocks  = {
	nrats: 2,
	rats: clocks,
};
#endif

static void snd_es1688_set_rate(es1688_t *chip, snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int bits, divider;

	if (runtime->rate_num == clocks[0].num)
		bits = 256 - runtime->rate_den;
	else
		bits = 128 - runtime->rate_den;
	/* set filter register */
	divider = 256 - 7160000*20/(8*82*runtime->rate);
	/* write result to hardware */
	snd_es1688_write(chip, 0xa1, bits);
	snd_es1688_write(chip, 0xa2, divider);
}

static int snd_es1688_ioctl(snd_pcm_substream_t * substream,
			    unsigned int cmd, void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_es1688_trigger(es1688_t *chip, int cmd, unsigned char value)
{
	int val;

	if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		value = 0x00;
	} else if (cmd != SNDRV_PCM_TRIGGER_START) {
		return -EINVAL;
	}
	spin_lock(&chip->reg_lock);
	chip->trigger_value = value;
	val = snd_es1688_read(chip, 0xb8);
	if ((val < 0) || (val & 0x0f) == value) {
		spin_unlock(&chip->reg_lock);
		return -EINVAL;	/* something is wrong */
	}
#if 0
	printk("trigger: val = 0x%x, value = 0x%x\n", val, value);
	printk("trigger: residue = 0x%x\n", get_dma_residue(chip->dma8));
#endif
	snd_es1688_write(chip, 0xb8, (val & 0xf0) | value);
	spin_unlock(&chip->reg_lock);
	return 0;
}

static int snd_es1688_hw_params(snd_pcm_substream_t * substream,
				snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_es1688_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_es1688_playback_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	es1688_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma_size = size;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1688_reset(chip);
	snd_es1688_set_rate(chip, substream);
	snd_es1688_write(chip, 0xb8, 4);	/* auto init DMA mode */
	snd_es1688_write(chip, 0xa8, (snd_es1688_read(chip, 0xa8) & ~0x03) | (3 - runtime->channels));
	snd_es1688_write(chip, 0xb9, 2);	/* demand mode (4 bytes/request) */
	if (runtime->channels == 1) {
		if (snd_pcm_format_width(runtime->format) == 8) {
			/* 8. bit mono */
			snd_es1688_write(chip, 0xb6, 0x80);
			snd_es1688_write(chip, 0xb7, 0x51);
			snd_es1688_write(chip, 0xb7, 0xd0);
		} else {
			/* 16. bit mono */
			snd_es1688_write(chip, 0xb6, 0x00);
			snd_es1688_write(chip, 0xb7, 0x71);
			snd_es1688_write(chip, 0xb7, 0xf4);
		}
	} else {
		if (snd_pcm_format_width(runtime->format) == 8) {
			/* 8. bit stereo */
			snd_es1688_write(chip, 0xb6, 0x80);
			snd_es1688_write(chip, 0xb7, 0x51);
			snd_es1688_write(chip, 0xb7, 0x98);
		} else {
			/* 16. bit stereo */
			snd_es1688_write(chip, 0xb6, 0x00);
			snd_es1688_write(chip, 0xb7, 0x71);
			snd_es1688_write(chip, 0xb7, 0xbc);
		}
	}
	snd_es1688_write(chip, 0xb1, (snd_es1688_read(chip, 0xb1) & 0x0f) | 0x50);
	snd_es1688_write(chip, 0xb2, (snd_es1688_read(chip, 0xb2) & 0x0f) | 0x50);
	snd_es1688_dsp_command(chip, ES1688_DSP_CMD_SPKON);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	/* --- */
	count = -count;
	snd_dma_program(chip->dma8, runtime->dma_area, size, DMA_MODE_WRITE | DMA_AUTOINIT);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1688_write(chip, 0xa4, (unsigned char) count);
	snd_es1688_write(chip, 0xa5, (unsigned char) (count >> 8));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_es1688_playback_trigger(snd_pcm_substream_t * substream,
				       int cmd)
{
	es1688_t *chip = snd_pcm_substream_chip(substream);
	return snd_es1688_trigger(chip, cmd, 0x05);
}

static int snd_es1688_capture_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	es1688_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma_size = size;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1688_reset(chip);
	snd_es1688_set_rate(chip, substream);
	snd_es1688_dsp_command(chip, ES1688_DSP_CMD_SPKOFF);
	snd_es1688_write(chip, 0xb8, 0x0e);	/* auto init DMA mode */
	snd_es1688_write(chip, 0xa8, (snd_es1688_read(chip, 0xa8) & ~0x03) | (3 - runtime->channels));
	snd_es1688_write(chip, 0xb9, 2);	/* demand mode (4 bytes/request) */
	if (runtime->channels == 1) {
		if (snd_pcm_format_width(runtime->format) == 8) {
			/* 8. bit mono */
			snd_es1688_write(chip, 0xb7, 0x51);
			snd_es1688_write(chip, 0xb7, 0xd0);
		} else {
			/* 16. bit mono */
			snd_es1688_write(chip, 0xb7, 0x71);
			snd_es1688_write(chip, 0xb7, 0xf4);
		}
	} else {
		if (snd_pcm_format_width(runtime->format) == 8) {
			/* 8. bit stereo */
			snd_es1688_write(chip, 0xb7, 0x51);
			snd_es1688_write(chip, 0xb7, 0x98);
		} else {
			/* 16. bit stereo */
			snd_es1688_write(chip, 0xb7, 0x71);
			snd_es1688_write(chip, 0xb7, 0xbc);
		}
	}
	snd_es1688_write(chip, 0xb1, (snd_es1688_read(chip, 0xb1) & 0x0f) | 0x50);
	snd_es1688_write(chip, 0xb2, (snd_es1688_read(chip, 0xb2) & 0x0f) | 0x50);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	/* --- */
	count = -count;
	snd_dma_program(chip->dma8, runtime->dma_area, size, DMA_MODE_READ | DMA_AUTOINIT);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_es1688_write(chip, 0xa4, (unsigned char) count);
	snd_es1688_write(chip, 0xa5, (unsigned char) (count >> 8));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_es1688_capture_trigger(snd_pcm_substream_t * substream,
				      int cmd)
{
	es1688_t *chip = snd_pcm_substream_chip(substream);
	return snd_es1688_trigger(chip, cmd, 0x0f);
}

void snd_es1688_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	es1688_t *chip = snd_magic_cast(es1688_t, dev_id, return);

	if (chip->trigger_value == 0x05)	/* ok.. playback is active */
		snd_pcm_period_elapsed(chip->playback_substream);
	if (chip->trigger_value == 0x0f)	/* ok.. capture is active */
		snd_pcm_period_elapsed(chip->capture_substream);

	inb(ES1688P(chip, DATA_AVAIL));	/* ack interrupt */
}

static snd_pcm_uframes_t snd_es1688_playback_pointer(snd_pcm_substream_t * substream)
{
	es1688_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
	
	if (chip->trigger_value != 0x05)
		return 0;
	ptr = chip->dma_size - snd_dma_residue(chip->dma8);
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_es1688_capture_pointer(snd_pcm_substream_t * substream)
{
	es1688_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
	
	if (chip->trigger_value != 0x0f)
		return 0;
	ptr = chip->dma_size - snd_dma_residue(chip->dma8);
	return bytes_to_frames(substream->runtime, ptr);
}

/*

 */

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_es1688_playback =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	65536,
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	65536,
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

static snd_pcm_hardware_t snd_es1688_capture =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	65536,
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	65536,
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};
#else
static snd_pcm_hardware_t snd_es1688_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	65536,
	period_bytes_min:	64,
	period_bytes_max:	65536,
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_es1688_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	65536,
	period_bytes_min:	64,
	period_bytes_max:	65536,
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};
#endif

/*

 */

static int snd_es1688_playback_open(snd_pcm_substream_t * substream)
{
	es1688_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (chip->capture_substream != NULL)
		return -EAGAIN;
	chip->playback_substream = substream;
	runtime->hw = snd_es1688_playback;
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &hw_constraints_clocks);
	return 0;
}

static int snd_es1688_capture_open(snd_pcm_substream_t * substream)
{
	es1688_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (chip->playback_substream != NULL)
		return -EAGAIN;
	chip->capture_substream = substream;
	runtime->hw = snd_es1688_capture;
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &hw_constraints_clocks);
	return 0;
}

static int snd_es1688_playback_close(snd_pcm_substream_t * substream)
{
	es1688_t *chip = snd_pcm_substream_chip(substream);

	chip->playback_substream = NULL;
	return 0;
}

static int snd_es1688_capture_close(snd_pcm_substream_t * substream)
{
	es1688_t *chip = snd_pcm_substream_chip(substream);

	chip->capture_substream = NULL;
	return 0;
}

static int snd_es1688_free(es1688_t *chip)
{
	if (chip->res_port) {
		snd_es1688_init(chip, 0);
		release_resource(chip->res_port);
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *) chip);
	if (chip->dma8 >= 0) {
		disable_dma(chip->dma8);
		free_dma(chip->dma8);
	}
	snd_magic_kfree(chip);
	return 0;
}

static int snd_es1688_dev_free(snd_device_t *device)
{
	es1688_t *chip = snd_magic_cast(es1688_t, device->device_data, return -ENXIO);
	return snd_es1688_free(chip);
}

static const char *snd_es1688_chip_id(es1688_t *chip)
{
	static char tmp[16];
	sprintf(tmp, "ES%s688 rev %i", chip->hardware == ES1688_HW_688 ? "" : "1", chip->version & 0x0f);
	return tmp;
}

int snd_es1688_create(snd_card_t * card,
		      unsigned long port,
		      unsigned long mpu_port,
		      int irq,
		      int mpu_irq,
		      int dma8,
		      unsigned short hardware,
		      es1688_t **rchip)
{
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_es1688_dev_free,0,0
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_es1688_dev_free,
	};
#endif                                
	es1688_t *chip;
	int err;

	*rchip = NULL;
	chip = snd_magic_kcalloc(es1688_t, 0, GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	chip->irq = -1;
	chip->dma8 = -1;
	
	if ((chip->res_port = request_region(port + 4, 12, "ES1688")) == NULL) {
		snd_es1688_free(chip);
		return -EBUSY;
	}
	if (request_irq(irq, snd_es1688_interrupt, SA_INTERRUPT, "ES1688", (void *) chip)) {
		snd_es1688_free(chip);
		return -EBUSY;
	}
	chip->irq = irq;
	if (request_dma(dma8, "ES1688")) {
		snd_es1688_free(chip);
		return -EBUSY;
	}
	chip->dma8 = dma8;

	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->mixer_lock);
	chip->card = card;
	chip->port = port;
	mpu_port &= ~0x000f;
	if (mpu_port < 0x300 || mpu_port > 0x330)
		mpu_port = 0;
	chip->mpu_port = mpu_port;
	chip->mpu_irq = mpu_irq;
	chip->hardware = hardware;

	if ((err = snd_es1688_probe(chip)) < 0) {
		snd_es1688_free(chip);
		return err;
	}
	if ((err = snd_es1688_init(chip, 1)) < 0) {
		snd_es1688_free(chip);
		return err;
	}

	/* Register device */
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_es1688_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_es1688_playback_ops = {
	snd_es1688_playback_open,
	snd_es1688_playback_close,
	snd_es1688_ioctl,
	snd_es1688_hw_params,
	snd_es1688_hw_free,
	snd_es1688_playback_prepare,
	snd_es1688_playback_trigger,
	snd_es1688_playback_pointer,0,0
};

static snd_pcm_ops_t snd_es1688_capture_ops = {
	snd_es1688_capture_open,
	snd_es1688_capture_close,
	snd_es1688_ioctl,
	snd_es1688_hw_params,
	snd_es1688_hw_free,
	snd_es1688_capture_prepare,
	snd_es1688_capture_trigger,
	snd_es1688_capture_pointer,
};
#else
static snd_pcm_ops_t snd_es1688_playback_ops = {
	open:			snd_es1688_playback_open,
	close:			snd_es1688_playback_close,
	ioctl:			snd_es1688_ioctl,
	hw_params:		snd_es1688_hw_params,
	hw_free:		snd_es1688_hw_free,
	prepare:		snd_es1688_playback_prepare,
	trigger:		snd_es1688_playback_trigger,
	pointer:		snd_es1688_playback_pointer,
};

static snd_pcm_ops_t snd_es1688_capture_ops = {
	open:			snd_es1688_capture_open,
	close:			snd_es1688_capture_close,
	ioctl:			snd_es1688_ioctl,
	hw_params:		snd_es1688_hw_params,
	hw_free:		snd_es1688_hw_free,
	prepare:		snd_es1688_capture_prepare,
	trigger:		snd_es1688_capture_trigger,
	pointer:		snd_es1688_capture_pointer,
};
#endif

static void snd_es1688_pcm_free(snd_pcm_t *pcm)
{
	es1688_t *chip = snd_magic_cast(es1688_t, pcm->private_data, return);
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int snd_es1688_pcm(es1688_t * chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "ESx688", device, 1, 1, &pcm)) < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_es1688_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_es1688_capture_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_es1688_pcm_free;
	pcm->info_flags = SNDRV_PCM_INFO_HALF_DUPLEX;
	sprintf(pcm->name, snd_es1688_chip_id(chip));
	chip->pcm = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, 64*1024, 64*1024, GFP_KERNEL|GFP_DMA);

	if (rpcm)
		*rpcm = NULL;
	return 0;
}

/*
 *  MIXER part
 */

static int snd_es1688_info_mux(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[9] = {
		"Mic", "Mic Master", "CD", "AOUT",
		"Mic1", "Mix", "Line", "Master"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 8;
	if (uinfo->value.enumerated.item > 7)
		uinfo->value.enumerated.item = 7;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_es1688_get_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es1688_t *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = snd_es1688_mixer_read(chip, ES1688_REC_DEV) & 7;
	return 0;
}

static int snd_es1688_put_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es1688_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char oval, nval;
	int change;
	
	if (ucontrol->value.enumerated.item[0] > 8)
		return -EINVAL;
	spin_lock_irqsave(&chip->reg_lock, flags);
	oval = snd_es1688_mixer_read(chip, ES1688_REC_DEV);
	nval = (ucontrol->value.enumerated.item[0] & 7) | (oval & ~15);
	change = nval != oval;
	if (change)
		snd_es1688_mixer_write(chip, ES1688_REC_DEV, nval);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

#ifdef TARGET_OS2
#define ES1688_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0,0,xname, xindex, \
  0,snd_es1688_info_single, \
  snd_es1688_get_single, snd_es1688_put_single, \
  reg | (shift << 8) | (mask << 16) | (invert << 24) }
#else
#define ES1688_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_es1688_info_single, \
  get: snd_es1688_get_single, put: snd_es1688_put_single, \
  private_value: reg | (shift << 8) | (mask << 16) | (invert << 24) }
#endif

static int snd_es1688_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_es1688_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es1688_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.integer.value[0] = (snd_es1688_mixer_read(chip, reg) >> shift) & mask;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

static int snd_es1688_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es1688_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	int change;
	unsigned char oval, nval;
	
	nval = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		nval = mask - nval;
	nval <<= shift;
	spin_lock_irqsave(&chip->reg_lock, flags);
	oval = snd_es1688_mixer_read(chip, reg);
	nval = (oval & ~(mask << shift)) | nval;
	change = nval != oval;
	if (change)
		snd_es1688_mixer_write(chip, reg, nval);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

#ifdef TARGET_OS2
#define ES1688_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0,0,xname, xindex, \
  0,snd_es1688_info_double, \
  snd_es1688_get_double, snd_es1688_put_double, \
  left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }
#else
#define ES1688_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_es1688_info_double, \
  get: snd_es1688_get_double, put: snd_es1688_put_double, \
  private_value: left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }
#endif

static int snd_es1688_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_es1688_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es1688_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	unsigned char left, right;
	
	spin_lock_irqsave(&chip->reg_lock, flags);
	if (left_reg < 0xa0)
		left = snd_es1688_mixer_read(chip, left_reg);
	else
		left = snd_es1688_read(chip, left_reg);
	if (left_reg != right_reg) {
		if (right_reg < 0xa0) 
			right = snd_es1688_mixer_read(chip, right_reg);
		else
			right = snd_es1688_read(chip, right_reg);
	} else
		right = left;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	ucontrol->value.integer.value[0] = (left >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (right >> shift_right) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

static int snd_es1688_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es1688_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	unsigned char val1, val2, oval1, oval2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;
	spin_lock_irqsave(&chip->reg_lock, flags);
	if (left_reg != right_reg) {
		if (left_reg < 0xa0)
			oval1 = snd_es1688_mixer_read(chip, left_reg);
		else
			oval1 = snd_es1688_read(chip, left_reg);
		if (right_reg < 0xa0)
			oval2 = snd_es1688_mixer_read(chip, right_reg);
		else
			oval2 = snd_es1688_read(chip, right_reg);
		val1 = (oval1 & ~(mask << shift_left)) | val1;
		val2 = (oval2 & ~(mask << shift_right)) | val2;
		change = val1 != oval1 || val2 != oval2;
		if (change) {
			if (left_reg < 0xa0)
				snd_es1688_mixer_write(chip, left_reg, val1);
			else
				snd_es1688_write(chip, left_reg, val1);
			if (right_reg < 0xa0)
				snd_es1688_mixer_write(chip, right_reg, val1);
			else
				snd_es1688_write(chip, right_reg, val1);
		}
	} else {
		if (left_reg < 0xa0)
			oval1 = snd_es1688_mixer_read(chip, left_reg);
		else
			oval1 = snd_es1688_read(chip, left_reg);
		val1 = (oval1 & ~((mask << shift_left) | (mask << shift_right))) | val1 | val2;
		change = val1 != oval1;
		if (change) {
			if (left_reg < 0xa0)
				snd_es1688_mixer_write(chip, left_reg, val1);
			else
				snd_es1688_write(chip, left_reg, val1);
		}
			
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

#define ES1688_CONTROLS (sizeof(snd_es1688_controls)/sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_es1688_controls[] = {
ES1688_DOUBLE("Master Playback Volume", 0, ES1688_MASTER_DEV, ES1688_MASTER_DEV, 4, 0, 15, 0),
ES1688_DOUBLE("PCM Playback Volume", 0, ES1688_PCM_DEV, ES1688_PCM_DEV, 4, 0, 15, 0),
ES1688_DOUBLE("Line Playback Volume", 0, ES1688_LINE_DEV, ES1688_LINE_DEV, 4, 0, 15, 0),
ES1688_DOUBLE("CD Playback Volume", 0, ES1688_CD_DEV, ES1688_CD_DEV, 4, 0, 15, 0),
ES1688_DOUBLE("FM Playback Volume", 0, ES1688_FM_DEV, ES1688_FM_DEV, 4, 0, 15, 0),
ES1688_DOUBLE("Mic Playback Volume", 0, ES1688_MIC_DEV, ES1688_MIC_DEV, 4, 0, 15, 0),
ES1688_DOUBLE("Aux Playback Volume", 0, ES1688_AUX_DEV, ES1688_AUX_DEV, 4, 0, 15, 0),
ES1688_SINGLE("PC Speaker Playback Volume", 0, ES1688_SPEAKER_DEV, 0, 7, 0),
ES1688_DOUBLE("Capture Volume", 0, ES1688_RECLEV_DEV, ES1688_RECLEV_DEV, 4, 0, 15, 0),
ES1688_SINGLE("Capture Switch", 0, ES1688_REC_DEV, 4, 1, 1),
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Capture Source",0,0,
	snd_es1688_info_mux,
	snd_es1688_get_mux,
	snd_es1688_put_mux,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Capture Source",
	info: snd_es1688_info_mux,
	get: snd_es1688_get_mux,
	put: snd_es1688_put_mux,
#endif
},
};

#define ES1688_INIT_TABLE_SIZE (sizeof(snd_es1688_init_table)/2)

static unsigned char snd_es1688_init_table[][2] = {
	{ ES1688_MASTER_DEV, 0 },
	{ ES1688_PCM_DEV, 0 },
	{ ES1688_LINE_DEV, 0 },
	{ ES1688_CD_DEV, 0 },
	{ ES1688_FM_DEV, 0 },
	{ ES1688_MIC_DEV, 0 },
	{ ES1688_AUX_DEV, 0 },
	{ ES1688_SPEAKER_DEV, 0 },
	{ ES1688_RECLEV_DEV, 0 },
	{ ES1688_REC_DEV, 0x17 }
};
                                        
int snd_es1688_mixer(es1688_t *chip)
{
	snd_card_t *card;
	int err, idx;
	unsigned char reg, val;

	snd_assert(chip != NULL && chip->card != NULL, return -EINVAL);

	card = chip->card;

	strcpy(card->mixername, snd_es1688_chip_id(chip));

	for (idx = 0; idx < ES1688_CONTROLS; idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_es1688_controls[idx], chip))) < 0)
			return err;
	}
	for (idx = 0; idx < ES1688_INIT_TABLE_SIZE; idx++) {
		reg = snd_es1688_init_table[idx][0];
		val = snd_es1688_init_table[idx][1];
		if (reg < 0xa0)
			snd_es1688_mixer_write(chip, reg, val);
		else
			snd_es1688_write(chip, reg, val);
	}
	return 0;
}

EXPORT_SYMBOL(snd_es1688_mixer_write);
EXPORT_SYMBOL(snd_es1688_mixer_read);
EXPORT_SYMBOL(snd_es1688_interrupt);
EXPORT_SYMBOL(snd_es1688_create);
EXPORT_SYMBOL(snd_es1688_pcm);
EXPORT_SYMBOL(snd_es1688_mixer);

/*
 *  INIT part
 */

static int __init alsa_es1688_init(void)
{
	return 0;
}

static void __exit alsa_es1688_exit(void)
{
}

module_init(alsa_es1688_init)
module_exit(alsa_es1688_exit)
