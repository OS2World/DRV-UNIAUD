/*
 *  Driver for generic ESS AudioDrive ES18xx soundcards
 *  Copyright (c) by Christian Fischbach <fishbach@pool.informatik.rwth-aachen.de>
 *  Copyright (c) by Abramo Bagnara <abramo@alsa-project.org>
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
/* GENERAL NOTES:
 *
 * BUGS:
 * - There are pops (we can't delay in trigger function, cause midlevel 
 *   often need to trigger down and then up very quickly).
 *   Any ideas?
 * - Support for 16 bit DMA seems to be broken. I've no hardware to tune it.
 */

/*
 * ES1868  NOTES:
 * - The chip has one half duplex pcm (with very limited full duplex support).
 *
 * - Duplex stereophonic sound is impossible.
 * - Record and playback must share the same frequency rate.
 *
 * - The driver use dma2 for playback and dma1 for capture.
 */

/*
 * ES1869 NOTES:
 *
 * - there are a first full duplex pcm and a second playback only pcm
 *   (incompatible with first pcm capture)
 * 
 * - there is support for the capture volume and ESS Spatializer 3D effect.
 *
 * - contrarily to some pages in DS_1869.PDF the rates can be set
 *   independently.
 *
 * BUGS:
 *
 * - There is a major trouble I noted:
 *
 *   using both channel for playback stereo 16 bit samples at 44100 Hz
 *   the second pcm (Audio1) DMA slows down irregularly and sound is garbled.
 *   
 *   The same happens using Audio1 for captureing.
 *
 *   The Windows driver does not suffer of this (although it use Audio1
 *   only for captureing). I'm unable to discover why.
 *
 */


#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#define SNDRV_GET_ID
#include <sound/initval.h>

struct _snd_es18xx {
	unsigned long port;		/* port of ESS chip */
	unsigned long mpu_port;		/* MPU-401 port of ESS chip */
	unsigned long fm_port;		/* FM port */
	unsigned long ctrl_port;	/* Control port of ESS chip */
	struct resource *res_port;
	struct resource *res_mpu_port;
	struct resource *res_ctrl_port;
	int irq;			/* IRQ number of ESS chip */
	int dma1;			/* DMA1 */
	int dma2;			/* DMA2 */
	unsigned short version;		/* version of ESS chip */
	int caps;			/* Chip capabilities */
	unsigned short audio2_vol;	/* volume level of audio2 */

	unsigned short active;		/* active channel mask */
	unsigned int dma1_size;
	unsigned int dma2_size;
	unsigned int dma1_shift;
	unsigned int dma2_shift;

	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_pcm_substream_t *playback_a_substream;
	snd_pcm_substream_t *capture_a_substream;
	snd_pcm_substream_t *playback_b_substream;

	snd_rawmidi_t *rmidi;

	snd_kcontrol_t *hw_volume;
	snd_kcontrol_t *hw_switch;
	snd_kcontrol_t *master_volume;
	snd_kcontrol_t *master_switch;

	spinlock_t reg_lock;
	spinlock_t mixer_lock;
	spinlock_t ctrl_lock;
};

#define AUDIO1_IRQ	0x01
#define AUDIO2_IRQ	0x02
#define HWV_IRQ		0x04
#define MPU_IRQ		0x08

#define ES18XX_PCM2	0x0001	/* Has two useable PCM */
#define ES18XX_SPATIALIZER 0x0002	/* Has 3D Spatializer */
#define ES18XX_RECMIX	0x0004	/* Has record mixer */
#define ES18XX_DUPLEX_MONO 0x0008	/* Has mono duplex only */
#define ES18XX_DUPLEX_SAME 0x0010	/* Playback and record must share the same rate */
#define ES18XX_NEW_RATE	0x0020	/* More precise rate setting */
#define ES18XX_AUXB	0x0040	/* AuxB mixer control */
#define ES18XX_HWV	0x0080	/* Has hardware volume */
#define ES18XX_MONO	0x0100	/* Mono_in mixer control */
#define ES18XX_I2S	0x0200	/* I2S mixer control */
#define ES18XX_MUTEREC	0x0400	/* Record source can be muted */
#define ES18XX_CONTROL	0x0800	/* Has control ports */

typedef struct _snd_es18xx es18xx_t;

#define chip_t es18xx_t

/* Lowlevel */

#define DAC1 0x01
#define ADC1 0x02
#define DAC2 0x04
#define MILLISECOND 10000

static int snd_es18xx_dsp_command(es18xx_t *chip, unsigned char val)
{
        int i;

        for(i = MILLISECOND; i; i--)
                if ((inb(chip->port + 0x0C) & 0x80) == 0) {
                        outb(val, chip->port + 0x0C);
                        return 0;
                }
        snd_printk("dsp_command: timeout (0x%x)\n", val);
        return -EINVAL;
}

static int snd_es18xx_dsp_get_byte(es18xx_t *chip)
{
        int i;

        for(i = MILLISECOND/10; i; i--)
                if (inb(chip->port + 0x0C) & 0x40)
                        return inb(chip->port + 0x0A);
        snd_printk("dsp_get_byte failed: 0x%lx = 0x%x!!!\n", chip->port + 0x0A, inb(chip->port + 0x0A));
        return -ENODEV;
}

#undef REG_DEBUG

static int snd_es18xx_write(es18xx_t *chip,
			    unsigned char reg, unsigned char data)
{
	unsigned long flags;
	int ret;
	
        spin_lock_irqsave(&chip->reg_lock, flags);
	ret = snd_es18xx_dsp_command(chip, reg);
	if (ret < 0)
		goto end;
        ret = snd_es18xx_dsp_command(chip, data);
 end:
        spin_unlock_irqrestore(&chip->reg_lock, flags);
#ifdef REG_DEBUG
	snd_printk("Reg %02x set to %02x\n", reg, data);
#endif
	return ret;
}

static int snd_es18xx_read(es18xx_t *chip, unsigned char reg)
{
	unsigned long flags;
	int ret, data;
        spin_lock_irqsave(&chip->reg_lock, flags);
	ret = snd_es18xx_dsp_command(chip, 0xC0);
	if (ret < 0)
		goto end;
        ret = snd_es18xx_dsp_command(chip, reg);
	if (ret < 0)
		goto end;
	data = snd_es18xx_dsp_get_byte(chip);
	ret = data;
#ifdef REG_DEBUG
	snd_printk("Reg %02x now is %02x (%d)\n", reg, data, ret);
#endif
 end:
        spin_unlock_irqrestore(&chip->reg_lock, flags);
	return ret;
}

/* Return old value */
static int snd_es18xx_bits(es18xx_t *chip, unsigned char reg,
			   unsigned char mask, unsigned char val)
{
        int ret;
	unsigned char old, new, oval;
	unsigned long flags;
        spin_lock_irqsave(&chip->reg_lock, flags);
        ret = snd_es18xx_dsp_command(chip, 0xC0);
	if (ret < 0)
		goto end;
        ret = snd_es18xx_dsp_command(chip, reg);
	if (ret < 0)
		goto end;
	ret = snd_es18xx_dsp_get_byte(chip);
	if (ret < 0) {
		goto end;
	}
	old = ret;
	oval = old & mask;
	if (val != oval) {
		ret = snd_es18xx_dsp_command(chip, reg);
		if (ret < 0)
			goto end;
		new = (old & ~mask) | (val & mask);
		ret = snd_es18xx_dsp_command(chip, new);
		if (ret < 0)
			goto end;
#ifdef REG_DEBUG
		snd_printk("Reg %02x was %02x, set to %02x (%d)\n", reg, old, new, ret);
#endif
	}
	ret = oval;
 end:
        spin_unlock_irqrestore(&chip->reg_lock, flags);
	return ret;
}

inline void snd_es18xx_mixer_write(es18xx_t *chip,
			    unsigned char reg, unsigned char data)
{
	unsigned long flags;
        spin_lock_irqsave(&chip->mixer_lock, flags);
        outb(reg, chip->port + 0x04);
        outb(data, chip->port + 0x05);
        spin_unlock_irqrestore(&chip->mixer_lock, flags);
#ifdef REG_DEBUG
	snd_printk("Mixer reg %02x set to %02x\n", reg, data);
#endif
}

inline int snd_es18xx_mixer_read(es18xx_t *chip, unsigned char reg)
{
	unsigned long flags;
	int data;
        spin_lock_irqsave(&chip->mixer_lock, flags);
        outb(reg, chip->port + 0x04);
	data = inb(chip->port + 0x05);
        spin_unlock_irqrestore(&chip->mixer_lock, flags);
#ifdef REG_DEBUG
	snd_printk("Mixer reg %02x now is %02x\n", reg, data);
#endif
        return data;
}

/* Return old value */
static inline int snd_es18xx_mixer_bits(es18xx_t *chip, unsigned char reg,
					unsigned char mask, unsigned char val)
{
	unsigned char old, new, oval;
	unsigned long flags;
        spin_lock_irqsave(&chip->mixer_lock, flags);
        outb(reg, chip->port + 0x04);
	old = inb(chip->port + 0x05);
	oval = old & mask;
	if (val != oval) {
		new = (old & ~mask) | (val & mask);
		outb(new, chip->port + 0x05);
#ifdef REG_DEBUG
		snd_printk("Mixer reg %02x was %02x, set to %02x\n", reg, old, new);
#endif
	}
        spin_unlock_irqrestore(&chip->mixer_lock, flags);
	return oval;
}

static inline int snd_es18xx_mixer_writable(es18xx_t *chip, unsigned char reg,
					    unsigned char mask)
{
	int old, expected, new;
	unsigned long flags;
        spin_lock_irqsave(&chip->mixer_lock, flags);
        outb(reg, chip->port + 0x04);
	old = inb(chip->port + 0x05);
	expected = old ^ mask;
	outb(expected, chip->port + 0x05);
	new = inb(chip->port + 0x05);
        spin_unlock_irqrestore(&chip->mixer_lock, flags);
#ifdef REG_DEBUG
	snd_printk("Mixer reg %02x was %02x, set to %02x, now is %02x\n", reg, old, expected, new);
#endif
	return expected == new;
}


static int snd_es18xx_reset(es18xx_t *chip)
{
	int i;
        outb(0x03, chip->port + 0x06);
        inb(chip->port + 0x06);
        outb(0x00, chip->port + 0x06);
        for(i = 0; i < MILLISECOND && !(inb(chip->port + 0x0E) & 0x80); i++);
        if (inb(chip->port + 0x0A) != 0xAA)
                return -1;
	return 0;
}

static int snd_es18xx_reset_fifo(es18xx_t *chip)
{
        outb(0x02, chip->port + 0x06);
        inb(chip->port + 0x06);
        outb(0x00, chip->port + 0x06);
	return 0;
}

#ifdef TARGET_OS2
static ratnum_t new_clocks[2] = {
	{
/*		num: */ 793800,
/*		den_min: */ 1,
/*		den_max: */ 128,
/*		den_step: */ 1,
	},
	{
/*		num: */ 768000,
/*		den_min: */ 1,
/*		den_max: */ 128,
/*		den_step: */ 1,
	}
};

static snd_pcm_hw_constraint_ratnums_t new_hw_constraints_clocks = {
/*	nrats: */ 2,
/*	rats: */ new_clocks,
};

static ratnum_t old_clocks[2] = {
	{
/*		num: */ 795444,
/*		den_min: */ 1,
/*		den_max: */ 128,
/*		den_step: */ 1,
	},
	{
/*		num: */ 397722,
/*		den_min: */ 1,
/*		den_max: */ 128,
/*		den_step: */ 1,
	}
};

static snd_pcm_hw_constraint_ratnums_t old_hw_constraints_clocks  = {
/*	nrats: */ 2,
/*	rats: */ old_clocks,
};
#else
static ratnum_t new_clocks[2] = {
	{
		num: 793800,
		den_min: 1,
		den_max: 128,
		den_step: 1,
	},
	{
		num: 768000,
		den_min: 1,
		den_max: 128,
		den_step: 1,
	}
};

static snd_pcm_hw_constraint_ratnums_t new_hw_constraints_clocks = {
	nrats: 2,
	rats: new_clocks,
};

static ratnum_t old_clocks[2] = {
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

static snd_pcm_hw_constraint_ratnums_t old_hw_constraints_clocks  = {
	nrats: 2,
	rats: old_clocks,
};
#endif

static void snd_es18xx_rate_set(es18xx_t *chip, 
				snd_pcm_substream_t *substream,
				int mode)
{
	unsigned int bits, div0;
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (chip->caps & ES18XX_NEW_RATE) {
		if (runtime->rate_num == new_clocks[0].num)
			bits = 128 - runtime->rate_den;
		else
			bits = 256 - runtime->rate_den;
	} else {
		if (runtime->rate_num == old_clocks[0].num)
			bits = 256 - runtime->rate_den;
		else
			bits = 128 - runtime->rate_den;
	}

	/* set filter register */
	div0 = 256 - 7160000*20/(8*82*runtime->rate);
		
	if ((chip->caps & ES18XX_PCM2) && mode == DAC2) {
		snd_es18xx_mixer_write(chip, 0x70, bits);
		snd_es18xx_mixer_write(chip, 0x72, div0);
	} else {
		snd_es18xx_write(chip, 0xA1, bits);
		snd_es18xx_write(chip, 0xA2, div0);
	}
}

static int snd_es18xx_playback_hw_params(snd_pcm_substream_t * substream,
					 snd_pcm_hw_params_t * hw_params)
{
	es18xx_t *chip = snd_pcm_substream_chip(substream);
	int shift, err;

	shift = 0;
	if (params_channels(hw_params) == 2)
		shift++;
	if (snd_pcm_format_width(params_format(hw_params)) == 16)
		shift++;

	switch (substream->number) {
	case 0:
		if ((chip->caps & ES18XX_DUPLEX_MONO) &&
		    (chip->capture_a_substream) &&
		    params_channels(hw_params) != 1) {
			_snd_pcm_hw_param_setempty(hw_params, SNDRV_PCM_HW_PARAM_CHANNELS);
			return -EBUSY;
		}
		chip->dma2_shift = shift;
		break;
	case 1:
		chip->dma1_shift = shift;
		break;
	}
	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	return 0;
}

static int snd_es18xx_playback1_prepare(es18xx_t *chip,
					snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma2_size = size;

        snd_es18xx_rate_set(chip, substream, DAC2);

        /* Transfer Count Reload */
        count = 0x10000 - count;
        snd_es18xx_mixer_write(chip, 0x74, count & 0xff);
        snd_es18xx_mixer_write(chip, 0x76, count >> 8);

	/* Set format */
        snd_es18xx_mixer_bits(chip, 0x7A, 0x07,
			      ((runtime->channels == 1) ? 0x00 : 0x02) |
			      (snd_pcm_format_width(runtime->format) == 16 ? 0x01 : 0x00) |
			      (snd_pcm_format_unsigned(runtime->format) ? 0x00 : 0x04));

        /* Set DMA controller */
        snd_dma_program(chip->dma2, runtime->dma_area, size, DMA_MODE_WRITE | DMA_AUTOINIT);

	return 0;
}

static int snd_es18xx_playback1_trigger(es18xx_t *chip,
					snd_pcm_substream_t * substream,
					int cmd)
{
        if (cmd == SNDRV_PCM_TRIGGER_START) {
		if (chip->active & DAC2)
			return 0;
		chip->active |= DAC2;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		if (!(chip->active & DAC2))
			return 0;
		chip->active &= ~DAC2;
	} else {
		return -EINVAL;
	}

	if (cmd == SNDRV_PCM_TRIGGER_START) {	
                /* Start DMA */
		if (chip->dma2 >= 4)
			snd_es18xx_mixer_write(chip, 0x78, 0xb3);
		else
			snd_es18xx_mixer_write(chip, 0x78, 0x93);
#ifdef AVOID_POPS
		/* Avoid pops */
                udelay(100000);
		if (chip->caps & ES18XX_PCM2)
			/* Restore Audio 2 volume */
			snd_es18xx_mixer_write(chip, 0x7C, chip->audio2_vol);
		else
			/* Enable PCM output */
			snd_es18xx_dsp_command(chip, 0xD1);
#endif
        }
        else {
                /* Stop DMA */
                snd_es18xx_mixer_write(chip, 0x78, 0x00);
#ifdef AVOID_POPS
                udelay(25000);
		if (chip->caps & ES18XX_PCM2)
			/* Set Audio 2 volume to 0 */
			snd_es18xx_mixer_write(chip, 0x7C, 0);
		else
			/* Disable PCM output */
			snd_es18xx_dsp_command(chip, 0xD3);
#endif
        }

	return 0;
}

static int snd_es18xx_capture_hw_params(snd_pcm_substream_t * substream,
					snd_pcm_hw_params_t * hw_params)
{
	es18xx_t *chip = snd_pcm_substream_chip(substream);
	int shift, err;

	shift = 0;
	if ((chip->caps & ES18XX_DUPLEX_MONO) &&
	    chip->playback_a_substream &&
	    params_channels(hw_params) != 1) {
		_snd_pcm_hw_param_setempty(hw_params, SNDRV_PCM_HW_PARAM_CHANNELS);
		return -EBUSY;
	}
	if (params_channels(hw_params) == 2)
		shift++;
	if (snd_pcm_format_width(params_format(hw_params)) == 16)
		shift++;
	chip->dma1_shift = shift;
	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	return 0;
}

static int snd_es18xx_capture_prepare(snd_pcm_substream_t *substream)
{
        es18xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma1_size = size;

	snd_es18xx_reset_fifo(chip);

        /* Set stereo/mono */
        snd_es18xx_bits(chip, 0xA8, 0x03, runtime->channels == 1 ? 0x02 : 0x01);

        snd_es18xx_rate_set(chip, substream, ADC1);

        /* Transfer Count Reload */
	count = 0x10000 - count;
	snd_es18xx_write(chip, 0xA4, count & 0xff);
	snd_es18xx_write(chip, 0xA5, count >> 8);

#ifdef AVOID_POPS
	udelay(100000);
#endif

        /* Set format */
        snd_es18xx_write(chip, 0xB7, 
                         snd_pcm_format_unsigned(runtime->format) ? 0x51 : 0x71);
        snd_es18xx_write(chip, 0xB7, 0x90 |
                         ((runtime->channels == 1) ? 0x40 : 0x08) |
                         (snd_pcm_format_width(runtime->format) == 16 ? 0x04 : 0x00) |
                         (snd_pcm_format_unsigned(runtime->format) ? 0x00 : 0x20));

        /* Set DMA controler */
        snd_dma_program(chip->dma1, runtime->dma_area, size, DMA_MODE_READ | DMA_AUTOINIT);

	return 0;
}

static int snd_es18xx_capture_trigger(snd_pcm_substream_t *substream,
				      int cmd)
{
        es18xx_t *chip = snd_pcm_substream_chip(substream);

        if (cmd == SNDRV_PCM_TRIGGER_START) {
		if (chip->active & ADC1)
			return 0;
		chip->active |= ADC1;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		if (!(chip->active & ADC1))
			return 0;
		chip->active &= ~ADC1;
	} else {
		return -EINVAL;
	}

	if (cmd == SNDRV_PCM_TRIGGER_START)
                /* Start DMA */
                snd_es18xx_write(chip, 0xB8, 0x0f);
        else
                /* Stop DMA */
                snd_es18xx_write(chip, 0xB8, 0x00);
	return 0;
}

static int snd_es18xx_playback2_prepare(es18xx_t *chip,
					snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	chip->dma1_size = size;

	snd_es18xx_reset_fifo(chip);

        /* Set stereo/mono */
        snd_es18xx_bits(chip, 0xA8, 0x03, runtime->channels == 1 ? 0x02 : 0x01);

        snd_es18xx_rate_set(chip, substream, DAC1);

        /* Transfer Count Reload */
	count = 0x10000 - count;
	snd_es18xx_write(chip, 0xA4, count & 0xff);
	snd_es18xx_write(chip, 0xA5, count >> 8);

        /* Set format */
        snd_es18xx_write(chip, 0xB6,
                         snd_pcm_format_unsigned(runtime->format) ? 0x80 : 0x00);
        snd_es18xx_write(chip, 0xB7, 
                         snd_pcm_format_unsigned(runtime->format) ? 0x51 : 0x71);
        snd_es18xx_write(chip, 0xB7, 0x90 |
                         (runtime->channels == 1 ? 0x40 : 0x08) |
                         (snd_pcm_format_width(runtime->format) == 16 ? 0x04 : 0x00) |
                         (snd_pcm_format_unsigned(runtime->format) ? 0x00 : 0x20));

        /* Set DMA controler */
        snd_dma_program(chip->dma1, runtime->dma_area, size, DMA_MODE_WRITE | DMA_AUTOINIT);

	return 0;
}

static int snd_es18xx_playback2_trigger(es18xx_t *chip,
					snd_pcm_substream_t *substream,
					int cmd)
{
        if (cmd == SNDRV_PCM_TRIGGER_START) {
		if (chip->active & DAC1)
			return 0;
		chip->active |= DAC1;
        } else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		if (!(chip->active & DAC1))
			return 0;
		chip->active &= ~DAC1;
	} else {
		return -EINVAL;
	}

	if (cmd == SNDRV_PCM_TRIGGER_START) {
                /* Start DMA */
                snd_es18xx_write(chip, 0xB8, 0x05);
#ifdef AVOID_POPS
		/* Avoid pops */
                udelay(100000);
                /* Enable Audio 1 */
                snd_es18xx_dsp_command(chip, 0xD1);
#endif
        }
        else {
                /* Stop DMA */
                snd_es18xx_write(chip, 0xB8, 0x00);
#ifdef AVOID_POPS
		/* Avoid pops */
                udelay(25000);
                /* Disable Audio 1 */
                snd_es18xx_dsp_command(chip, 0xD3);
#endif
        }
	return 0;
}

static int snd_es18xx_playback_prepare(snd_pcm_substream_t *substream)
{
        es18xx_t *chip = snd_pcm_substream_chip(substream);
	switch (substream->number) {
	case 0:
		return snd_es18xx_playback1_prepare(chip, substream);
	case 1:
		return snd_es18xx_playback2_prepare(chip, substream);
	}
	return -EINVAL;
}

static int snd_es18xx_playback_trigger(snd_pcm_substream_t *substream,
				       int cmd)
{
        es18xx_t *chip = snd_pcm_substream_chip(substream);
	switch (substream->number) {
	case 0:
		return snd_es18xx_playback1_trigger(chip, substream, cmd);
	case 1:
		return snd_es18xx_playback2_trigger(chip, substream, cmd);
	}
	return -EINVAL;
}

static void snd_es18xx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	es18xx_t *chip = snd_magic_cast(es18xx_t, dev_id, return);
	unsigned char status;


	if (chip->caps & ES18XX_CONTROL) {
		/* Read Interrupt status */
		status = inb(chip->ctrl_port + 6);
	} else {
		/* Read Interrupt status */
		status = snd_es18xx_mixer_read(chip, 0x7f) >> 4;
	}
#if 0
	else {
		status = 0;
		if (inb(chip->port + 0x0C) & 0x01)
			status |= AUDIO1_IRQ;
		if (snd_es18xx_mixer_read(chip, 0x7A) & 0x80)
			status |= AUDIO2_IRQ;
		if ((chip->caps & ES18XX_HWV) &&
		    snd_es18xx_mixer_read(chip, 0x64) & 0x10)
			status |= HWV_IRQ;
	}
#endif

	/* Audio 1 & Audio 2 */
        if (status & AUDIO2_IRQ) {
                if (chip->active & DAC2)
                	snd_pcm_period_elapsed(chip->playback_a_substream);
		/* ack interrupt */
                snd_es18xx_mixer_bits(chip, 0x7A, 0x80, 0x00);
        }
        if (status & AUDIO1_IRQ) {
                /* ok.. capture is active */
                if (chip->active & ADC1)
                	snd_pcm_period_elapsed(chip->capture_a_substream);
                /* ok.. playback2 is active */
                else if (chip->active & DAC1)
                	snd_pcm_period_elapsed(chip->playback_b_substream);
		/* ack interrupt */
		inb(chip->port + 0x0E);
        }

	/* MPU */
	if ((status & MPU_IRQ) && chip->rmidi)
		snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data, regs);

	/* Hardware volume */
	if (status & HWV_IRQ) {
		int split = snd_es18xx_mixer_read(chip, 0x64) & 0x80;
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE, &chip->hw_switch->id);
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE, &chip->hw_volume->id);
		if (!split) {
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE, &chip->master_switch->id);
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE, &chip->master_volume->id);
		}
		/* ack interrupt */
		snd_es18xx_mixer_write(chip, 0x66, 0x00);
	}

}

static snd_pcm_uframes_t snd_es18xx_playback_pointer(snd_pcm_substream_t * substream)
{
        es18xx_t *chip = snd_pcm_substream_chip(substream);
	int pos;

	switch (substream->number) {
	case 0:
		if (!(chip->active & DAC2))
			return 0;
		pos = chip->dma2_size - snd_dma_residue(chip->dma2);
		return pos >> chip->dma2_shift;
	case 1:
		if (!(chip->active & DAC1))
			return 0;
		pos = chip->dma1_size - snd_dma_residue(chip->dma1);
		return pos >> chip->dma1_shift;
	}
	return 0;
}

static snd_pcm_uframes_t snd_es18xx_capture_pointer(snd_pcm_substream_t * substream)
{
        es18xx_t *chip = snd_pcm_substream_chip(substream);
	int pos;

        if (!(chip->active & ADC1))
                return 0;
	pos = chip->dma1_size - snd_dma_residue(chip->dma1);
	return pos >> chip->dma1_shift;
}

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_es18xx_playback =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
/*	formats:	  */	(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 | 
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE),
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max: */	65536,
/*	period_bytes_min: */	64,
/*	period_bytes_max: */	65536,
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

static snd_pcm_hardware_t snd_es18xx_capture =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
/*	formats:	  */	(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 | 
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE),
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max: */	65536,
/*	period_bytes_min: */	64,
/*	period_bytes_max: */	65536,
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};
#else
static snd_pcm_hardware_t snd_es18xx_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	formats:		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 | 
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE),
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

static snd_pcm_hardware_t snd_es18xx_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	formats:		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 | 
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE),
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

static int snd_es18xx_playback_open(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
        es18xx_t *chip = snd_pcm_substream_chip(substream);

	switch (substream->number) {
	case 0:
		if ((chip->caps & ES18XX_DUPLEX_MONO) &&
		    chip->capture_a_substream && 
		    chip->capture_a_substream->runtime->channels != 1)
			return -EAGAIN;
		chip->playback_a_substream = substream;
		break;
	case 1:
		if (chip->capture_a_substream)
			return -EAGAIN;
		chip->playback_b_substream = substream;
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}
	substream->runtime->hw = snd_es18xx_playback;
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      (chip->caps & ES18XX_NEW_RATE) ? &new_hw_constraints_clocks : &old_hw_constraints_clocks);
        return 0;
}

static int snd_es18xx_capture_open(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
        es18xx_t *chip = snd_pcm_substream_chip(substream);

        if (chip->playback_b_substream)
                return -EAGAIN;
	if ((chip->caps & ES18XX_DUPLEX_MONO) &&
	    chip->playback_a_substream &&
	    chip->playback_a_substream->runtime->channels != 1)
		return -EAGAIN;
        chip->capture_a_substream = substream;
	substream->runtime->hw = snd_es18xx_capture;
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      (chip->caps & ES18XX_NEW_RATE) ? &new_hw_constraints_clocks : &old_hw_constraints_clocks);
        return 0;
}

static int snd_es18xx_playback_close(snd_pcm_substream_t * substream)
{
        es18xx_t *chip = snd_pcm_substream_chip(substream);

	switch (substream->number) {
	case 0:
		chip->playback_a_substream = NULL;
		break;
	case 1:
		chip->playback_b_substream = NULL;
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}
	
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_es18xx_capture_close(snd_pcm_substream_t * substream)
{
        es18xx_t *chip = snd_pcm_substream_chip(substream);

        chip->capture_a_substream = NULL;
	snd_pcm_lib_free_pages(substream);
        return 0;
}

/*
 *  MIXER part
 */

static int snd_es18xx_info_mux(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[8] = {
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

static int snd_es18xx_get_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = snd_es18xx_mixer_read(chip, 0x1c) & 0x07;
	return 0;
}

static int snd_es18xx_put_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned char val = ucontrol->value.enumerated.item[0];
	
	if (val > 7)
		return -EINVAL;
	return snd_es18xx_mixer_bits(chip, 0x1c, 0x07, val) != val;
}

static int snd_es18xx_info_spatializer_enable(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_es18xx_get_spatializer_enable(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned char val = snd_es18xx_mixer_read(chip, 0x50);
	ucontrol->value.integer.value[0] = !!(val & 8);
	return 0;
}

static int snd_es18xx_put_spatializer_enable(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned char oval, nval;
	int change;
	nval = ucontrol->value.integer.value[0] ? 0x0c : 0x04;
	oval = snd_es18xx_mixer_read(chip, 0x50) & 0x0c;
	change = nval != oval;
	if (change) {
		snd_es18xx_mixer_write(chip, 0x50, nval & ~0x04);
		snd_es18xx_mixer_write(chip, 0x50, nval);
	}
	return change;
}

static int snd_es18xx_info_hw_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 63;
	return 0;
}

static int snd_es18xx_get_hw_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = snd_es18xx_mixer_read(chip, 0x61) & 0x3f;
	ucontrol->value.integer.value[1] = snd_es18xx_mixer_read(chip, 0x63) & 0x3f;
	return 0;
}

static int snd_es18xx_info_hw_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_es18xx_get_hw_switch(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = !(snd_es18xx_mixer_read(chip, 0x61) & 0x40);
	ucontrol->value.integer.value[1] = !(snd_es18xx_mixer_read(chip, 0x63) & 0x40);
	return 0;
}

static void snd_es18xx_hwv_free(snd_kcontrol_t *kcontrol)
{
	es18xx_t *chip = snd_magic_cast(es18xx_t, _snd_kcontrol_chip(kcontrol), return);
	chip->master_volume = NULL;
	chip->master_switch = NULL;
	chip->hw_volume = NULL;
	chip->hw_switch = NULL;
}

static int snd_es18xx_reg_bits(es18xx_t *chip, unsigned char reg,
			       unsigned char mask, unsigned char val)
{
	if (reg < 0xa0)
		return snd_es18xx_mixer_bits(chip, reg, mask, val);
	else
		return snd_es18xx_bits(chip, reg, mask, val);
}

static int snd_es18xx_reg_read(es18xx_t *chip, unsigned char reg)
{
	if (reg < 0xa0)
		return snd_es18xx_mixer_read(chip, reg);
	else
		return snd_es18xx_read(chip, reg);
}

#ifdef TARGET_OS2
#define ES18XX_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, xindex, \
  0, snd_es18xx_info_single, \
  snd_es18xx_get_single, snd_es18xx_put_single, \
  reg | (shift << 8) | (mask << 16) | (invert << 24) }
#else
#define ES18XX_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_es18xx_info_single, \
  get: snd_es18xx_get_single, put: snd_es18xx_put_single, \
  private_value: reg | (shift << 8) | (mask << 16) | (invert << 24) }
#endif

static int snd_es18xx_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_es18xx_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	int val;
	
	val = snd_es18xx_reg_read(chip, reg);
	ucontrol->value.integer.value[0] = (val >> shift) & mask;
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

static int snd_es18xx_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	unsigned char val;
	
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	mask <<= shift;
	val <<= shift;
	return snd_es18xx_reg_bits(chip, reg, mask, val) != val;
}

#ifdef TARGET_OS2
#define ES18XX_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, xindex, \
  0, snd_es18xx_info_double, \
  snd_es18xx_get_double, snd_es18xx_put_double, \
  left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }
#else
#define ES18XX_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_es18xx_info_double, \
  get: snd_es18xx_get_double, put: snd_es18xx_put_double, \
  private_value: left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }
#endif

static int snd_es18xx_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_es18xx_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	unsigned char left, right;
	
	left = snd_es18xx_reg_read(chip, left_reg);
	if (left_reg != right_reg)
		right = snd_es18xx_reg_read(chip, right_reg);
	else
		right = left;
	ucontrol->value.integer.value[0] = (left >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (right >> shift_right) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

static int snd_es18xx_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	es18xx_t *chip = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	unsigned char val1, val2, mask1, mask2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;
	mask1 = mask << shift_left;
	mask2 = mask << shift_right;
	if (left_reg != right_reg) {
		change = 0;
		if (snd_es18xx_reg_bits(chip, left_reg, mask1, val1) != val1)
			change = 1;
		if (snd_es18xx_reg_bits(chip, right_reg, mask2, val2) != val2)
			change = 1;
	} else {
		change = (snd_es18xx_reg_bits(chip, left_reg, mask1 | mask2, 
					      val1 | val2) != (val1 | val2));
	}
	return change;
}

static snd_kcontrol_new_t snd_es18xx_base_controls[] = {
ES18XX_DOUBLE("Master Playback Volume", 0, 0x60, 0x62, 0, 0, 63, 0),
ES18XX_DOUBLE("Master Playback Switch", 0, 0x60, 0x62, 6, 6, 1, 1),
ES18XX_DOUBLE("Line Playback Volume", 0, 0x3e, 0x3e, 4, 0, 15, 0),
ES18XX_DOUBLE("CD Playback Volume", 0, 0x38, 0x38, 4, 0, 15, 0),
ES18XX_DOUBLE("FM Playback Volume", 0, 0x36, 0x36, 4, 0, 15, 0),
ES18XX_DOUBLE("Mono Playback Volume", 0, 0x6d, 0x6d, 4, 0, 15, 0),
ES18XX_DOUBLE("Mic Playback Volume", 0, 0x1a, 0x1a, 4, 0, 15, 0),
ES18XX_DOUBLE("Aux Playback Volume", 0, 0x3a, 0x3a, 4, 0, 15, 0),
ES18XX_SINGLE("PC Speaker Playback Volume", 0, 0x3c, 0, 7, 0),
ES18XX_SINGLE("Record Monitor", 0, 0xa8, 3, 1, 0),
ES18XX_DOUBLE("Capture Volume", 0, 0xb4, 0xb4, 4, 0, 15, 0),
ES18XX_SINGLE("Capture Switch", 0, 0x1c, 4, 1, 1),
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,
        0,0,
	"Capture Source", 0, 0,
	snd_es18xx_info_mux,
	snd_es18xx_get_mux,
	snd_es18xx_put_mux, 0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Capture Source",
	info: snd_es18xx_info_mux,
	get: snd_es18xx_get_mux,
	put: snd_es18xx_put_mux,
#endif
}
};

static snd_kcontrol_new_t snd_es18xx_mono_in_control = 
ES18XX_DOUBLE("Mono Input Playback Volume", 0, 0x6d, 0x6d, 4, 0, 15, 0);

static snd_kcontrol_new_t snd_es18xx_recmix_controls[] = {
ES18XX_DOUBLE("PCM Capture Volume", 0, 0x69, 0x69, 4, 0, 15, 0),
ES18XX_DOUBLE("Mic Capture Volume", 0, 0x68, 0x68, 4, 0, 15, 0),
ES18XX_DOUBLE("Line Capture Volume", 0, 0x6e, 0x6e, 4, 0, 15, 0),
ES18XX_DOUBLE("FM Capture Volume", 0, 0x6b, 0x6b, 4, 0, 15, 0),
ES18XX_DOUBLE("Mono Capture Volume", 0, 0x6f, 0x6f, 4, 0, 15, 0),
ES18XX_DOUBLE("CD Capture Volume", 0, 0x6a, 0x6a, 4, 0, 15, 0),
ES18XX_DOUBLE("Aux Capture Volume", 0, 0x6c, 0x6c, 4, 0, 15, 0)
};

static snd_kcontrol_new_t snd_es18xx_pcm1_controls[] = {
ES18XX_DOUBLE("PCM Playback Volume", 0, 0x14, 0x14, 4, 0, 15, 0),
};

static snd_kcontrol_new_t snd_es18xx_pcm2_controls[] = {
ES18XX_DOUBLE("PCM Playback Volume", 0, 0x7c, 0x7c, 4, 0, 15, 0),
ES18XX_DOUBLE("PCM Playback Volume", 1, 0x14, 0x14, 4, 0, 15, 0)
};

static snd_kcontrol_new_t snd_es18xx_spatializer_controls[] = {
ES18XX_SINGLE("3D Control - Level", 0, 0x52, 0, 63, 0),
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,
        0,0,
	"3D Control - Switch", 0, 0,
	snd_es18xx_info_spatializer_enable,
	snd_es18xx_get_spatializer_enable,
	snd_es18xx_put_spatializer_enable,
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "3D Control - Switch",
	info: snd_es18xx_info_spatializer_enable,
	get: snd_es18xx_get_spatializer_enable,
	put: snd_es18xx_put_spatializer_enable,
#endif
}
};

static snd_kcontrol_new_t snd_es18xx_micpre1_control = 
ES18XX_SINGLE("Mic Boost (+26dB)", 0, 0xa9, 2, 1, 0);

static snd_kcontrol_new_t snd_es18xx_micpre2_control =
ES18XX_SINGLE("Mic Boost (+26dB)", 0, 0x7d, 3, 1, 0);

static snd_kcontrol_new_t snd_es18xx_hw_volume_controls[] = {
#ifdef TARGET_OS2
{
	SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0,
	"Hardware Master Playback Volume",0,
	SNDRV_CTL_ELEM_ACCESS_READ,
	snd_es18xx_info_hw_volume,
	snd_es18xx_get_hw_volume,0, 0
},
{
	SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0,
	"Hardware Master Playback Switch",0,
	SNDRV_CTL_ELEM_ACCESS_READ,
	snd_es18xx_info_hw_switch,
	snd_es18xx_get_hw_switch,0, 0
},
#else
{
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Hardware Master Playback Volume",
	access: SNDRV_CTL_ELEM_ACCESS_READ,
	info: snd_es18xx_info_hw_volume,
	get: snd_es18xx_get_hw_volume,
},
{
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Hardware Master Playback Switch",
	access: SNDRV_CTL_ELEM_ACCESS_READ,
	info: snd_es18xx_info_hw_switch,
	get: snd_es18xx_get_hw_switch,
},
#endif
ES18XX_SINGLE("Hardware Master Volume Split", 0, 0x64, 7, 1, 0),
};

#if 0
static int __init snd_es18xx_config_read(es18xx_t *chip, unsigned char reg)
{
	int data;
	unsigned long flags;
        spin_lock_irqsave(&chip->ctrl_lock, flags);
	outb(reg, chip->ctrl_port);
	data = inb(chip->ctrl_port + 1);
        spin_unlock_irqrestore(&chip->ctrl_lock, flags);
	return data;
}
#endif

static void __init snd_es18xx_config_write(es18xx_t *chip, 
					   unsigned char reg, unsigned char data)
{
	/* No need for spinlocks, this function is used only in
	   otherwise protected init code */
	outb(reg, chip->ctrl_port);
	outb(data, chip->ctrl_port + 1);
#ifdef REG_DEBUG
	snd_printk("Config reg %02x set to %02x\n", reg, data);
#endif
}

static int __init snd_es18xx_initialize(es18xx_t *chip)
{
	int mask = 0;

        /* enable extended mode */
        snd_es18xx_dsp_command(chip, 0xC6);
	/* Reset mixer registers */
	snd_es18xx_mixer_write(chip, 0x00, 0x00);

        /* Audio 1 DMA demand mode (4 bytes/request) */
        snd_es18xx_write(chip, 0xB9, 2);
	if (chip->caps & ES18XX_CONTROL) {
		/* Hardware volume IRQ */
		snd_es18xx_config_write(chip, 0x27, chip->irq);
		if (chip->fm_port > SNDRV_AUTO_PORT) {
			/* FM I/O */
			snd_es18xx_config_write(chip, 0x62, chip->fm_port >> 8);
			snd_es18xx_config_write(chip, 0x63, chip->fm_port & 0xff);
		}
		if (chip->mpu_port > SNDRV_AUTO_PORT) {
			/* MPU-401 I/O */
			snd_es18xx_config_write(chip, 0x64, chip->mpu_port >> 8);
			snd_es18xx_config_write(chip, 0x65, chip->mpu_port & 0xff);
			/* MPU-401 IRQ */
			snd_es18xx_config_write(chip, 0x28, chip->irq);
		}
		/* Audio1 IRQ */
		snd_es18xx_config_write(chip, 0x70, chip->irq);
		/* Audio2 IRQ */
		snd_es18xx_config_write(chip, 0x72, chip->irq);
		/* Audio1 DMA */
		snd_es18xx_config_write(chip, 0x74, chip->dma1);
		/* Audio2 DMA */
		snd_es18xx_config_write(chip, 0x75, chip->dma2);

		/* Enable Audio 1 IRQ */
		snd_es18xx_write(chip, 0xB1, 0x50);
		/* Enable Audio 2 IRQ */
		snd_es18xx_mixer_write(chip, 0x7A, 0x40);
		/* Enable Audio 1 DMA */
		snd_es18xx_write(chip, 0xB2, 0x50);
		/* Enable MPU and hardware volume interrupt */
		snd_es18xx_mixer_write(chip, 0x64, 0x42);
	}
	else {
		int irqmask, dma1mask, dma2mask;
		switch (chip->irq) {
		case 2:
		case 9:
			irqmask = 0;
			break;
		case 5:
			irqmask = 1;
			break;
		case 7:
			irqmask = 2;
			break;
		case 10:
			irqmask = 3;
			break;
		default:
			snd_printk("invalid irq %d\n", chip->irq);
			return -ENODEV;
		}
		switch (chip->dma1) {
		case 0:
			dma1mask = 1;
			break;
		case 1:
			dma1mask = 2;
			break;
		case 3:
			dma1mask = 3;
			break;
		default:
			snd_printk("invalid dma1 %d\n", chip->dma1);
			return -ENODEV;
		}
		switch (chip->dma2) {
		case 0:
			dma2mask = 0;
			break;
		case 1:
			dma2mask = 1;
			break;
		case 3:
			dma2mask = 2;
			break;
		case 5:
			dma2mask = 3;
			break;
		default:
			snd_printk("invalid dma2 %d\n", chip->dma2);
			return -ENODEV;
		}

		/* Enable and set Audio 1 IRQ */
		snd_es18xx_write(chip, 0xB1, 0x50 | (irqmask << 2));
		/* Enable and set Audio 1 DMA */
		snd_es18xx_write(chip, 0xB2, 0x50 | (dma1mask << 2));
		/* Set Audio 2 DMA */
		snd_es18xx_mixer_bits(chip, 0x7d, 0x07, 0x04 | dma2mask);
		/* Enable Audio 2 IRQ and DMA
		   Set capture mixer input */
		snd_es18xx_mixer_write(chip, 0x7A, 0x68);
		/* Enable and set hardware volume interrupt */
		snd_es18xx_mixer_write(chip, 0x64, 0x06);
		if (chip->mpu_port > SNDRV_AUTO_PORT) {
			/* MPU401 share irq with audio
			   Joystick enabled
			   FM enabled */
			snd_es18xx_mixer_write(chip, 0x40, 0x43 | (chip->mpu_port & 0xf0) >> 1);
		}
		snd_es18xx_mixer_write(chip, 0x7f, ((irqmask + 1) << 1) | 0x01);
	}
	if (chip->caps & ES18XX_NEW_RATE) {
		/* Change behaviour of register A1
		   4x oversampling
		   2nd channel DAC asynchronous */
		snd_es18xx_mixer_write(chip, 0x71, 0x32);
	}
	if (!(chip->caps & ES18XX_PCM2)) {
		/* Enable DMA FIFO */
		snd_es18xx_write(chip, 0xB7, 0x80);
	}
	if (chip->caps & ES18XX_SPATIALIZER) {
		/* Set spatializer parameters to recommended values */
		snd_es18xx_mixer_write(chip, 0x54, 0x8f);
		snd_es18xx_mixer_write(chip, 0x56, 0x95);
		snd_es18xx_mixer_write(chip, 0x58, 0x94);
		snd_es18xx_mixer_write(chip, 0x5a, 0x80);
	}
	/* Mute input source */
	if (chip->caps & ES18XX_MUTEREC)
		mask = 0x10;
	if (chip->caps & ES18XX_RECMIX)
		snd_es18xx_mixer_write(chip, 0x1c, 0x05 | mask);
	else {
		snd_es18xx_mixer_write(chip, 0x1c, 0x00 | mask);
		snd_es18xx_write(chip, 0xb4, 0x00);
	}
#ifndef AVOID_POPS
	/* Enable PCM output */
	snd_es18xx_dsp_command(chip, 0xD1);
#endif

        return 0;
}

static int __init snd_es18xx_identify(es18xx_t *chip)
{
	int hi,lo;

	/* reset */
	if (snd_es18xx_reset(chip) < 0) {
                snd_printk("reset at 0x%lx failed!!!\n", chip->port);
		return -ENODEV;
	}

	snd_es18xx_dsp_command(chip, 0xe7);
	hi = snd_es18xx_dsp_get_byte(chip);
	if (hi < 0) {
		return hi;
	}
	lo = snd_es18xx_dsp_get_byte(chip);
	if ((lo & 0xf0) != 0x80) {
		return -ENODEV;
	}
	if (hi == 0x48) {
		chip->version = 0x488;
		return 0;
	}
	if (hi != 0x68) {
		return -ENODEV;
	}
	if ((lo & 0x0f) < 8) {
		chip->version = 0x688;
		return 0;
	}
			
        outb(0x40, chip->port + 0x04);
	hi = inb(chip->port + 0x05);
	lo = inb(chip->port + 0x05);
	if (hi != lo) {
		chip->version = hi << 8 | lo;
		chip->ctrl_port = inb(chip->port + 0x05) << 8;
		chip->ctrl_port += inb(chip->port + 0x05);

		if ((chip->res_ctrl_port = request_region(chip->ctrl_port, 8, "ES18xx - CTRL")) == NULL)
			return -EBUSY;

		return 0;
	}

	/* If has Hardware volume */
	if (snd_es18xx_mixer_writable(chip, 0x64, 0x04)) {
		/* If has Audio2 */
		if (snd_es18xx_mixer_writable(chip, 0x70, 0x7f)) {
			/* If has volume count */
			if (snd_es18xx_mixer_writable(chip, 0x64, 0x20)) {
				chip->version = 0x1887;
			} else {
				chip->version = 0x1888;
			}
		} else {
			chip->version = 0x1788;
		}
	}
	else
		chip->version = 0x1688;
	return 0;
}

static int __init snd_es18xx_probe(es18xx_t *chip)
{
	if (snd_es18xx_identify(chip) < 0) {
                snd_printk("[0x%lx] ESS chip not found\n", chip->port);
                return -ENODEV;
	}

	switch (chip->version) {
	case 0x1868:
		chip->caps = ES18XX_DUPLEX_MONO | ES18XX_DUPLEX_SAME | ES18XX_CONTROL | ES18XX_HWV;
		break;
	case 0x1869:
		chip->caps = ES18XX_PCM2 | ES18XX_SPATIALIZER | ES18XX_RECMIX | ES18XX_NEW_RATE | ES18XX_AUXB | ES18XX_MONO | ES18XX_MUTEREC | ES18XX_CONTROL | ES18XX_HWV;
		break;
	case 0x1878:
		chip->caps = ES18XX_DUPLEX_MONO | ES18XX_DUPLEX_SAME | ES18XX_I2S | ES18XX_CONTROL | ES18XX_HWV;
		break;
	case 0x1879:
		chip->caps = ES18XX_PCM2 | ES18XX_SPATIALIZER | ES18XX_RECMIX | ES18XX_NEW_RATE | ES18XX_AUXB | ES18XX_I2S | ES18XX_CONTROL | ES18XX_HWV;
		break;
	case 0x1887:
		chip->caps = ES18XX_PCM2 | ES18XX_RECMIX | ES18XX_AUXB | ES18XX_DUPLEX_SAME | ES18XX_HWV;
		break;
	case 0x1888:
		chip->caps = ES18XX_PCM2 | ES18XX_RECMIX | ES18XX_AUXB | ES18XX_DUPLEX_SAME | ES18XX_HWV;
		break;
	default:
                snd_printk("[0x%lx] unsupported chip ES%x\n",
                           chip->port, chip->version);
                return -ENODEV;
        }

        snd_printd("[0x%lx] ESS%x chip found\n", chip->port, chip->version);

        return snd_es18xx_initialize(chip);
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_es18xx_playback_ops = {
/*	open:	  */	snd_es18xx_playback_open,
/*	close:	  */	snd_es18xx_playback_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_es18xx_playback_hw_params,
        0,
/*	prepare:  */	snd_es18xx_playback_prepare,
/*	trigger:  */	snd_es18xx_playback_trigger,
/*	pointer:  */	snd_es18xx_playback_pointer,
        0, 0
};

static snd_pcm_ops_t snd_es18xx_capture_ops = {
/*	open:	  */	snd_es18xx_capture_open,
/*	close:	  */	snd_es18xx_capture_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_es18xx_capture_hw_params,
        0,
/*	prepare:  */	snd_es18xx_capture_prepare,
/*	trigger:  */	snd_es18xx_capture_trigger,
/*	pointer:  */	snd_es18xx_capture_pointer,
        0, 0
};
#else
static snd_pcm_ops_t snd_es18xx_playback_ops = {
	open:		snd_es18xx_playback_open,
	close:		snd_es18xx_playback_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_es18xx_playback_hw_params,
	prepare:	snd_es18xx_playback_prepare,
	trigger:	snd_es18xx_playback_trigger,
	pointer:	snd_es18xx_playback_pointer,
};

static snd_pcm_ops_t snd_es18xx_capture_ops = {
	open:		snd_es18xx_capture_open,
	close:		snd_es18xx_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_es18xx_capture_hw_params,
	prepare:	snd_es18xx_capture_prepare,
	trigger:	snd_es18xx_capture_trigger,
	pointer:	snd_es18xx_capture_pointer,
};
#endif

static void snd_es18xx_pcm_free(snd_pcm_t *pcm)
{
	es18xx_t *codec = snd_magic_cast(es18xx_t, pcm->private_data, return);
	codec->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int __init snd_es18xx_pcm(es18xx_t *chip, int device, snd_pcm_t ** rpcm)
{
        snd_pcm_t *pcm;
	char str[16];
	int err;

	if (rpcm)
		*rpcm = NULL;
	sprintf(str, "ES%x", chip->version);
	if (chip->caps & ES18XX_PCM2) {
		err = snd_pcm_new(chip->card, str, device, 2, 1, &pcm);
	} else {
		err = snd_pcm_new(chip->card, str, device, 1, 1, &pcm);
	}
        if (err < 0)
                return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_es18xx_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_es18xx_capture_ops);

	/* global setup */
        pcm->private_data = chip;
	pcm->private_free = snd_es18xx_pcm_free;
        pcm->info_flags = 0;
	if (chip->caps & ES18XX_DUPLEX_SAME)
		pcm->info_flags |= SNDRV_PCM_INFO_JOINT_DUPLEX;
	sprintf(pcm->name, "ESS AudioDrive ES%x", chip->version);
        chip->pcm = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, 64*1024, chip->dma1 > 3 || chip->dma2 > 3 ? 128*1024 : 64*1024, GFP_KERNEL|GFP_DMA);

        if (rpcm)
        	*rpcm = pcm;
	return 0;
}

static int snd_es18xx_free(es18xx_t *chip)
{
	if (chip->res_port)
		release_resource(chip->res_port);
	if (chip->res_ctrl_port)
		release_resource(chip->res_ctrl_port);
	if (chip->res_mpu_port)
		release_resource(chip->res_mpu_port);
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *) chip);
	if (chip->dma1 >= 0) {
		disable_dma(chip->dma1);
		free_dma(chip->dma1);
	}
	if (chip->dma2 >= 0) {
		disable_dma(chip->dma2);
		free_dma(chip->dma2);
	}
	snd_magic_kfree(chip);
	return 0;
}

static int snd_es18xx_dev_free(snd_device_t *device)
{
	es18xx_t *chip = snd_magic_cast(es18xx_t, device->device_data, return -ENXIO);
	return snd_es18xx_free(chip);
}

static int __init snd_es18xx_new_device(snd_card_t * card,
					unsigned long port,
					unsigned long mpu_port,
					unsigned long fm_port,
					int irq, int dma1, int dma2,
					es18xx_t ** rchip)
{
        es18xx_t *chip;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_es18xx_dev_free,0,0
        };
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_es18xx_dev_free,
        };
#endif
	int err;

	*rchip = NULL;
        chip = snd_magic_kcalloc(es18xx_t, 0, GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	spin_lock_init(&chip->reg_lock);
 	spin_lock_init(&chip->mixer_lock);
 	spin_lock_init(&chip->ctrl_lock);
        chip->card = card;
        chip->port = port;
        chip->mpu_port = mpu_port;
        chip->fm_port = fm_port;
        chip->irq = -1;
        chip->dma1 = -1;
        chip->dma2 = -1;
        chip->audio2_vol = 0x00;
	chip->active = 0;

	if ((chip->res_port = request_region(port, 16, "ES18xx")) == NULL) {
		snd_es18xx_free(chip);
		snd_printk("unable to grap ports 0x%lx-0x%lx\n", port, port + 16 - 1);
		return -EBUSY;
	}

	if (mpu_port > SNDRV_AUTO_PORT && (chip->res_mpu_port = request_region(mpu_port, 2, "ES18xx - MPU401")) == NULL) {
		snd_es18xx_free(chip);
		snd_printk("unable to grap MPU401 ports 0x%lx-0x%lx\n", mpu_port, mpu_port + 2 - 1);
		return -EBUSY;
	}

	if (request_irq(irq, snd_es18xx_interrupt, SA_INTERRUPT, "ES18xx", (void *) chip)) {
		snd_es18xx_free(chip);
		snd_printk("unable to grap IRQ %d\n", irq);
		return -EBUSY;
	}
	chip->irq = irq;

	if (request_dma(dma1, "ES18xx DMA 1")) {
		snd_es18xx_free(chip);
		snd_printk("unable to grap DMA1 %d\n", dma1);
		return -EBUSY;
	}
	chip->dma1 = dma1;

	if (request_dma(dma2, "ES18xx DMA 2")) {
		snd_es18xx_free(chip);
		snd_printk("unable to grap DMA2 %d\n", dma2);
		return -EBUSY;
	}
	chip->dma2 = dma2;

        if (snd_es18xx_probe(chip) < 0) {
                snd_es18xx_free(chip);
                return -ENODEV;
        }
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_es18xx_free(chip);
		return err;
	}
        *rchip = chip;
        return 0;
}

static int __init snd_es18xx_mixer(es18xx_t *chip)
{
	snd_card_t *card;
	int err, idx;

	card = chip->card;

	strcpy(card->mixername, chip->pcm->name);

	for (idx = 0; idx < sizeof(snd_es18xx_base_controls) / 
		     sizeof(snd_es18xx_base_controls[0]); idx++) {
		snd_kcontrol_t *kctl;
		kctl = snd_ctl_new1(&snd_es18xx_base_controls[idx], chip);
		if (chip->caps & ES18XX_HWV) {
			switch (idx) {
			case 0:
				chip->master_volume = kctl;
				kctl->private_free = snd_es18xx_hwv_free;
				break;
			case 1:
				chip->master_switch = kctl;
				kctl->private_free = snd_es18xx_hwv_free;
				break;
			}
		}
		if ((err = snd_ctl_add(card, kctl)) < 0)
			return err;
	}
	if (chip->caps & ES18XX_PCM2) {
		for (idx = 0; idx < sizeof(snd_es18xx_pcm2_controls) / 
			     sizeof(snd_es18xx_pcm2_controls[0]); idx++) {
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_pcm2_controls[idx], chip))) < 0)
				return err;
		} 
	} else {
		for (idx = 0; idx < sizeof(snd_es18xx_pcm1_controls) / 
			     sizeof(snd_es18xx_pcm1_controls[0]); idx++) {
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_pcm1_controls[idx], chip))) < 0)
				return err;
		}
	}

	if (chip->caps & ES18XX_MONO) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_mono_in_control, chip))) < 0)
			return err;
	}
	if (chip->caps & ES18XX_RECMIX) {
		for (idx = 0; idx < sizeof(snd_es18xx_recmix_controls) / 
			     sizeof(snd_es18xx_recmix_controls[0]); idx++) {
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_recmix_controls[idx], chip))) < 0)
				return err;
		}
	}
	switch (chip->version) {
	default:
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_micpre1_control, chip))) < 0)
			return err;
	case 0x1869:
	case 0x1879:
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_micpre2_control, chip))) < 0)
			return err;
	}
	if (chip->caps & ES18XX_SPATIALIZER) {
		for (idx = 0; idx < sizeof(snd_es18xx_spatializer_controls) / 
			     sizeof(snd_es18xx_spatializer_controls[0]); idx++) {
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_spatializer_controls[idx], chip))) < 0)
				return err;
		}
	}
	if (chip->caps & ES18XX_HWV) {
		for (idx = 0; idx < sizeof(snd_es18xx_hw_volume_controls) / 
			     sizeof(snd_es18xx_hw_volume_controls[0]); idx++) {
			snd_kcontrol_t *kctl;
			kctl = snd_ctl_new1(&snd_es18xx_hw_volume_controls[idx], chip);
			if (idx == 0)
				chip->hw_volume = kctl;
			else
				chip->hw_switch = kctl;
			kctl->private_free = snd_es18xx_hwv_free;
			if ((err = snd_ctl_add(card, kctl)) < 0)
				return err;
			
		}
	}
	return 0;
}
       

/* Card level */

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("ESS ES18xx AudioDrive");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{ESS,ES1868 PnP AudioDrive},"
		"{ESS,ES1869 PnP AudioDrive},"
		"{ESS,ES1878 PnP AudioDrive},"
		"{ESS,ES1879 PnP AudioDrive},"
		"{ESS,ES1887 PnP AudioDrive},"
		"{ESS,ES1888 PnP AudioDrive},"
		"{ESS,ES1887 AudioDrive}"
		"{ESS,ES1888 AudioDrive}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
#ifdef __ISAPNP__
#ifdef TARGET_OS2
static int snd_isapnp[SNDRV_CARDS] = {1,1,1,1,1,1,1,1};
#else
static int snd_isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
#endif
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260,0x280 */
#ifndef __ISAPNP__
#ifdef TARGET_OS2
static long snd_mpu_port[SNDRV_CARDS] = {-1,-1,-1,-1,-1,-1,-1,-1};
#else
static long snd_mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
#endif
#else
static long snd_mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
#endif
static long snd_fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int snd_dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */
static int snd_dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for ES18xx soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for ES18xx soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable ES18xx soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
#ifdef __ISAPNP__
MODULE_PARM(snd_isapnp, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_isapnp, "ISA PnP detection for specified soundcard.");
MODULE_PARM_SYNTAX(snd_isapnp, SNDRV_ISAPNP_DESC);
#endif
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for ES18xx driver.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_ENABLED ",allows:{{0x220,0x280,0x20}},prefers:{0x220},base:16,dialog:list");
MODULE_PARM(snd_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_mpu_port, "MPU-401 port # for ES18xx driver.");
MODULE_PARM_SYNTAX(snd_mpu_port, SNDRV_ENABLED ",allows:{{0x300,0x330,0x30},{0x800,0xffe,0x2}},prefers:{0x330,0x300},base:16,dialog:combo");
MODULE_PARM(snd_fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_fm_port, "FM port # for ES18xx driver.");
MODULE_PARM_SYNTAX(snd_fm_port, SNDRV_ENABLED ",allows:{{0x388},{0x800,0xffc,0x4}},prefers:{0x388},base:16,dialog:combo");
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for ES18xx driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_IRQ_DESC ",prefers:{5}");
MODULE_PARM(snd_dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma1, "DMA 1 # for ES18xx driver.");
MODULE_PARM_SYNTAX(snd_dma1, SNDRV_DMA8_DESC ",prefers:{1}");
MODULE_PARM(snd_dma2, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma2, "DMA 2 # for ES18xx driver.");
MODULE_PARM_SYNTAX(snd_dma2, SNDRV_ENABLED ",allows:{{0},{1},{3},{5}},dialog:list,prefers:{0}");

struct snd_audiodrive {
#ifdef __ISAPNP__
	struct isapnp_dev *dev;
	struct isapnp_dev *devc;
#endif
};

static snd_card_t *snd_audiodrive_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef __ISAPNP__

static struct isapnp_card *snd_audiodrive_isapnp_cards[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;
static const struct isapnp_card_id *snd_audiodrive_isapnp_id[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;

#ifdef TARGET_OS2
#define ISAPNP_ES18XX(_va, _vb, _vc, _device, _audio, _control) \
        { \
                0, ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
                { ISAPNP_DEVICE_ID(_va, _vb, _vc, _audio), \
                  ISAPNP_DEVICE_ID(_va, _vb, _vc, _control) } \
        }
#else
#define ISAPNP_ES18XX(_va, _vb, _vc, _device, _audio, _control) \
        { \
                ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
                devs : { ISAPNP_DEVICE_ID(_va, _vb, _vc, _audio), \
                         ISAPNP_DEVICE_ID(_va, _vb, _vc, _control) } \
        }
#endif

static struct isapnp_card_id snd_audiodrive_pnpids[] __devinitdata = {
	/* ESS 1868 (integrated on Compaq dual P-Pro motherboard and Genius 18PnP 3D) */
	ISAPNP_ES18XX('E','S','S',0x1868,0x1868,0x0000),
	/* ESS 1868 (integrated on Maxisound Cards) */
	ISAPNP_ES18XX('E','S','S',0x1868,0x8601,0x8600),
	/* ESS 1868 (integrated on Maxisound Cards) */
	ISAPNP_ES18XX('E','S','S',0x1868,0x8611,0x8610),
	/* ESS ES1869 Plug and Play AudioDrive */
	ISAPNP_ES18XX('E','S','S',0x0003,0x1869,0x0006),
	/* ESS 1869 */
	ISAPNP_ES18XX('E','S','S',0x1869,0x1869,0x0006),
	/* ESS 1878 */
	ISAPNP_ES18XX('E','S','S',0x1878,0x1878,0x0004),
	/* ESS 1879 */
	ISAPNP_ES18XX('E','S','S',0x1879,0x1879,0x0009),
	/* --- */
	{ ISAPNP_CARD_END, } /* end */
};

ISAPNP_CARD_TABLE(snd_audiodrive_pnpids);

static int __init snd_audiodrive_isapnp(int dev, struct snd_audiodrive *acard)
{
	const struct isapnp_card_id *id = snd_audiodrive_isapnp_id[dev];
	struct isapnp_card *card = snd_audiodrive_isapnp_cards[dev];
	struct isapnp_dev *pdev;

	acard->dev = isapnp_find_dev(card, id->devs[0].vendor, id->devs[0].function, NULL);
	if (acard->dev->active) {
		acard->dev = NULL;
		return -EBUSY;
	}
	acard->devc = isapnp_find_dev(card, id->devs[1].vendor, id->devs[1].function, NULL);
	if (acard->devc->active) {
		acard->dev = acard->devc = NULL;
		return -EBUSY;
	}
	/* Control port initialization */
	if (acard->devc->prepare(acard->devc)<0)
		return -EAGAIN;
	if (acard->devc->activate(acard->devc)<0) {
		snd_printk("isapnp control configure failure (out of resources?)\n");
		return -EAGAIN;
	}
	snd_printdd("isapnp: port=0x%lx\n", acard->devc->resource[0].start);
	/* PnP initialization */
	pdev = acard->dev;
	if (pdev->prepare(pdev)<0) {
		acard->devc->deactivate(acard->devc);
		return -EAGAIN;
	}
	if (snd_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], snd_port[dev], 16);
	if (snd_fm_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[1], snd_fm_port[dev], 4);
	if (snd_mpu_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[2], snd_mpu_port[dev], 2);
	if (snd_dma1[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[0], snd_dma1[dev], 1);
	if (snd_dma2[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[1], snd_dma2[dev], 1);
	if (snd_irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], snd_irq[dev], 1);
	if (pdev->activate(pdev)<0) {
		snd_printk("isapnp configure failure (out of resources?)\n");
		acard->devc->deactivate(acard->devc);
		return -EBUSY;
	}
	/* ok. hack using Vendor-Defined Card-Level registers */
	/* skip csn and logdev initialization - already done in isapnp_configure */
	isapnp_cfg_begin(pdev->bus->number, pdev->devfn);
	isapnp_write_byte(0x27, pdev->irq_resource[0].start);	/* Hardware Volume IRQ Number */
	if (snd_mpu_port[dev] > SNDRV_AUTO_PORT)
		isapnp_write_byte(0x28, pdev->irq);		/* MPU-401 IRQ Number */
	isapnp_write_byte(0x72, pdev->irq_resource[0].start);	/* second IRQ */
	isapnp_cfg_end();
	snd_port[dev] = pdev->resource[0].start;
	snd_fm_port[dev] = pdev->resource[1].start;
	snd_mpu_port[dev] = pdev->resource[2].start;
	snd_dma1[dev] = pdev->dma_resource[0].start;
	snd_dma2[dev] = pdev->dma_resource[1].start;
	snd_irq[dev] = pdev->irq_resource[0].start;
	snd_printdd("isapnp ES18xx: port=0x%lx, fm port=0x%lx, mpu port=0x%lx\n", snd_port[dev], snd_fm_port[dev], snd_mpu_port[dev]);
	snd_printdd("isapnp ES18xx: dma1=%i, dma2=%i, irq=%i\n", snd_dma1[dev], snd_dma2[dev], snd_irq[dev]);
	return 0;
}

static void snd_audiodrive_deactivate(struct snd_audiodrive *acard)
{
	if (acard->devc) {
		acard->devc->deactivate(acard->devc);
		acard->devc = NULL;
	}
	if (acard->dev) {
		acard->dev->deactivate(acard->dev);
		acard->dev = NULL;
	}
}
#endif /* __ISAPNP__ */

static void snd_audiodrive_free(snd_card_t *card)
{
	struct snd_audiodrive *acard = (struct snd_audiodrive *)card->private_data;

	if (acard) {
#ifdef __ISAPNP__
		snd_audiodrive_deactivate(acard);
#endif
	}
}

static int __init snd_audiodrive_probe(int dev)
{
	static int possible_irqs[] = {5, 9, 10, 7, 11, 12, -1};
	static int possible_dmas[] = {1, 0, 3, 5, -1};
	int irq, dma1, dma2;
	snd_card_t *card;
	struct snd_audiodrive *acard;
	snd_rawmidi_t *rmidi = NULL;
	es18xx_t *chip;
	opl3_t *opl3;
	int err;

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE,
			    sizeof(struct snd_audiodrive));
	if (card == NULL)
		return -ENOMEM;
	acard = (struct snd_audiodrive *)card->private_data;
	card->private_free = snd_audiodrive_free;
#ifdef __ISAPNP__
	if (snd_isapnp[dev] && (err = snd_audiodrive_isapnp(dev, acard)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif

	irq = snd_irq[dev];
	if (irq == SNDRV_AUTO_IRQ) {
		if ((irq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
        dma1 = snd_dma1[dev];
        if (dma1 == SNDRV_AUTO_DMA) {
                if ((dma1 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
                        snd_card_free(card);
                        snd_printk("unable to find a free DMA1\n");
                        return -EBUSY;
                }
        }
        dma2 = snd_dma2[dev];
        if (dma2 == SNDRV_AUTO_DMA) {
                if ((dma2 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
                        snd_card_free(card);
                        snd_printk("unable to find a free DMA2\n");
                        return -EBUSY;
                }
        }

	if ((err = snd_es18xx_new_device(card,
					 snd_port[dev],
					 snd_mpu_port[dev],
					 snd_fm_port[dev],
					 irq, dma1, dma2,
					 &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_es18xx_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_es18xx_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (snd_opl3_create(card, chip->fm_port, chip->fm_port + 2, OPL3_HW_OPL3, 0, &opl3) < 0) {
		snd_printk("opl3 not detected at 0x%lx\n", chip->port);
	} else {
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}

	if (snd_mpu_port[dev] > SNDRV_AUTO_PORT) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_ES18XX,
					       chip->mpu_port, 0,
					       irq, 0,
					       &rmidi)) < 0) {
			snd_card_free(card);
			return err;
		}
		chip->rmidi = rmidi;
	}
	sprintf(card->driver, "ES%x", chip->version);
	sprintf(card->shortname, "ESS AudioDrive ES%x", chip->version);
	sprintf(card->longname, "%s at 0x%lx, irq %d, dma1 %d, dma2 %d",
		card->shortname,
		chip->port,
		irq, dma1, dma2);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_audiodrive_cards[dev] = card;
	return 0;
}

static int __init snd_audiodrive_probe_legacy_port(unsigned long port)
{
	static int dev = 0;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev] || snd_port[dev] != SNDRV_AUTO_PORT)
			continue;
#ifdef __ISAPNP__
		if (snd_isapnp[dev])
			continue;
#endif
		snd_port[dev] = port;
		res = snd_audiodrive_probe(dev);
		if (res < 0)
			snd_port[dev] = SNDRV_AUTO_PORT;
		return res;
	}
	return -ENODEV;
}


#ifdef __ISAPNP__
static int __init snd_audiodrive_isapnp_detect(struct isapnp_card *card,
					       const struct isapnp_card_id *id)
{
	static int dev = 0;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev] || !snd_isapnp[dev])
			continue;
		snd_audiodrive_isapnp_cards[dev] = card;
                snd_audiodrive_isapnp_id[dev] = id;
                res = snd_audiodrive_probe(dev);
		if (res < 0)
			return res;
		dev++;
		return 0;
        }

        return -ENODEV;
}
#endif /* __ISAPNP__ */

static int __init alsa_card_es18xx_init(void)
{
	static unsigned long possible_ports[] = {0x220, 0x240, 0x260, 0x280, -1};
	int dev, cards = 0;

	/* legacy non-auto cards at first */
	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev] || snd_port[dev] == SNDRV_AUTO_PORT)
			continue;
#ifdef __ISAPNP__
		if (snd_isapnp[dev])
			continue;
#endif
		if (snd_audiodrive_probe(dev) >= 0)
			cards++;
	}
	/* legacy auto configured cards */
	cards += snd_legacy_auto_probe(possible_ports, snd_audiodrive_probe_legacy_port);
#ifdef __ISAPNP__
	/* ISA PnP cards at last */
	cards += isapnp_probe_cards(snd_audiodrive_pnpids, snd_audiodrive_isapnp_detect);
#endif
	if(!cards) {
#ifdef MODULE
		snd_printk("ESS AudioDrive ES18xx soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_es18xx_exit(void)
{
	int idx;

	for(idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_audiodrive_cards[idx]);
}

module_init(alsa_card_es18xx_init)
module_exit(alsa_card_es18xx_exit)

#ifndef MODULE

/* format is: snd-card-es18xx=snd_enable,snd_index,snd_id,snd_isapnp,
			      snd_port,snd_mpu_port,snd_fm_port,snd_irq,
			      snd_dma1,snd_dma2 */

static int __init alsa_card_es18xx_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;
	int __attribute__ ((__unused__)) pnp = INT_MAX;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,&pnp) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_mpu_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_fm_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma1[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma2[nr_dev]) == 2);
#ifdef __ISAPNP__
	if (pnp != INT_MAX)
		snd_isapnp[nr_dev] = pnp;
#endif
	nr_dev++;
	return 1;
}

__setup("snd-card-es18xx=", alsa_card_es18xx_setup);

#endif /* ifndef MODULE */
