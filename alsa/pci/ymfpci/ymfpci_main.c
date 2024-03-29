/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for control of YMF724/740/744/754 chips
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
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
#include <sound/control.h>
#include <sound/info.h>
#include <sound/ymfpci.h>

#define chip_t ymfpci_t

/*
 *  constants
 */

/*
 *  common I/O routines
 */

static void snd_ymfpci_irq_wait(ymfpci_t *chip);

static inline u8 snd_ymfpci_readb(ymfpci_t *chip, u32 offset)
{
	return readb(chip->reg_area_virt + offset);
}

static inline void snd_ymfpci_writeb(ymfpci_t *chip, u32 offset, u8 val)
{
	writeb(val, chip->reg_area_virt + offset);
}

static inline u16 snd_ymfpci_readw(ymfpci_t *chip, u32 offset)
{
	return readw(chip->reg_area_virt + offset);
}

static inline void snd_ymfpci_writew(ymfpci_t *chip, u32 offset, u16 val)
{
	writew(val, chip->reg_area_virt + offset);
}

static inline u32 snd_ymfpci_readl(ymfpci_t *chip, u32 offset)
{
	return readl(chip->reg_area_virt + offset);
}

static inline void snd_ymfpci_writel(ymfpci_t *chip, u32 offset, u32 val)
{
	writel(val, chip->reg_area_virt + offset);
}

static int snd_ymfpci_codec_ready(ymfpci_t *chip, int secondary, int sched)
{
	signed long end_time;
	u32 reg = secondary ? YDSXGR_SECSTATUSADR : YDSXGR_PRISTATUSADR;
	
	end_time = jiffies + 3 * (HZ / 4);
	do {
		if ((snd_ymfpci_readw(chip, reg) & 0x8000) == 0)
			return 0;
		if (sched) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
	} while (end_time - (signed long)jiffies >= 0);
	snd_printk("codec_ready: codec %i is not ready [0x%x]\n", secondary, snd_ymfpci_readw(chip, reg));
	return -EBUSY;
}

static void snd_ymfpci_codec_write(ac97_t *ac97, u16 reg, u16 val)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, ac97->private_data, return);
	u32 cmd;
	
	snd_ymfpci_codec_ready(chip, 0, 0);
	cmd = ((YDSXG_AC97WRITECMD | reg) << 16) | val;
	snd_ymfpci_writel(chip, YDSXGR_AC97CMDDATA, cmd);
}

static u16 snd_ymfpci_codec_read(ac97_t *ac97, u16 reg)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, ac97->private_data, return -ENXIO);

	if (snd_ymfpci_codec_ready(chip, 0, 0))
		return ~0;
	snd_ymfpci_writew(chip, YDSXGR_AC97CMDADR, YDSXG_AC97READCMD | reg);
	if (snd_ymfpci_codec_ready(chip, 0, 0))
		return ~0;
	if (chip->device_id == PCI_DEVICE_ID_YAMAHA_744 && chip->rev < 2) {
		int i;
		for (i = 0; i < 600; i++)
			snd_ymfpci_readw(chip, YDSXGR_PRISTATUSDATA);
	}
	return snd_ymfpci_readw(chip, YDSXGR_PRISTATUSDATA);
}

/*
 *  Misc routines
 */

static u32 snd_ymfpci_calc_delta(u32 rate)
{
	switch (rate) {
	case 8000:	return 0x02aaab00;
	case 11025:	return 0x03accd00;
	case 16000:	return 0x05555500;
	case 22050:	return 0x07599a00;
	case 32000:	return 0x0aaaab00;
	case 44100:	return 0x0eb33300;
	default:	return ((rate << 16) / 375) << 5;
	}
}

static u32 def_rate[8] = {
	100, 2000, 8000, 11025, 16000, 22050, 32000, 48000
};

static u32 snd_ymfpci_calc_lpfK(u32 rate)
{
	u32 i;
	static u32 val[8] = {
		0x00570000, 0x06AA0000, 0x18B20000, 0x20930000,
		0x2B9A0000, 0x35A10000, 0x3EAA0000, 0x40000000
	};
	
	if (rate == 44100)
		return 0x40000000;	/* FIXME: What's the right value? */
	for (i = 0; i < 8; i++)
		if (rate <= def_rate[i])
			return val[i];
	return val[0];
}

static u32 snd_ymfpci_calc_lpfQ(u32 rate)
{
	u32 i;
	static u32 val[8] = {
		0x35280000, 0x34A70000, 0x32020000, 0x31770000,
		0x31390000, 0x31C90000, 0x33D00000, 0x40000000
	};
	
	if (rate == 44100)
		return 0x370A0000;
	for (i = 0; i < 8; i++)
		if (rate <= def_rate[i])
			return val[i];
	return val[0];
}

/*
 *  Hardware start management
 */

static void snd_ymfpci_hw_start(ymfpci_t *chip)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->start_count++ > 1)
		goto __end;
	snd_ymfpci_writel(chip, YDSXGR_MODE,
			  snd_ymfpci_readl(chip, YDSXGR_MODE) | 3);
	chip->active_bank = snd_ymfpci_readl(chip, YDSXGR_CTRLSELECT) & 1;
      __end:
      	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_ymfpci_hw_stop(ymfpci_t *chip)
{
	unsigned long flags;
	long timeout = 1000;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (--chip->start_count > 0)
		goto __end;
	snd_ymfpci_writel(chip, YDSXGR_MODE,
			  snd_ymfpci_readl(chip, YDSXGR_MODE) & ~3);
	while (timeout-- > 0) {
		if ((snd_ymfpci_readl(chip, YDSXGR_STATUS) & 2) == 0)
			break;
	}
	if (atomic_read(&chip->interrupt_sleep_count)) {
		atomic_set(&chip->interrupt_sleep_count, 0);
		wake_up(&chip->interrupt_sleep);
	}
      __end:
      	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/*
 *  Playback voice management
 */

static int voice_alloc(ymfpci_t *chip, ymfpci_voice_type_t type, int pair, ymfpci_voice_t **rvoice)
{
	ymfpci_voice_t *voice, *voice2;
	int idx;
	
	*rvoice = NULL;
	for (idx = 0; idx < YDSXG_PLAYBACK_VOICES; idx += pair ? 2 : 1) {
		voice = &chip->voices[idx];
		voice2 = pair ? &chip->voices[idx+1] : NULL;
		if (voice->use || (voice2 && voice2->use))
			continue;
		voice->use = 1;
		if (voice2)
			voice2->use = 1;
		switch (type) {
		case YMFPCI_PCM:
			voice->pcm = 1;
			if (voice2)
				voice2->pcm = 1;
			break;
		case YMFPCI_SYNTH:
			voice->synth = 1;
			break;
		case YMFPCI_MIDI:
			voice->midi = 1;
			break;
		}
		snd_ymfpci_hw_start(chip);
		if (voice2)
			snd_ymfpci_hw_start(chip);
		*rvoice = voice;
		return 0;
	}
	return -ENOMEM;
}

int snd_ymfpci_voice_alloc(ymfpci_t *chip, ymfpci_voice_type_t type, int pair, ymfpci_voice_t **rvoice)
{
	unsigned long flags;
	int result;
	
	snd_assert(rvoice != NULL, return -EINVAL);
	snd_assert(!pair || type == YMFPCI_PCM, return -EINVAL);
	
	spin_lock_irqsave(&chip->voice_lock, flags);
	for (;;) {
		result = voice_alloc(chip, type, pair, rvoice);
		if (result == 0 || type != YMFPCI_PCM)
			break;
		/* TODO: synth/midi voice deallocation */
		break;
	}
	spin_unlock_irqrestore(&chip->voice_lock, flags);	
	return result;		
}

int snd_ymfpci_voice_free(ymfpci_t *chip, ymfpci_voice_t *pvoice)
{
	unsigned long flags;
	
	snd_assert(pvoice != NULL, return -EINVAL);
	snd_ymfpci_hw_stop(chip);
	spin_lock_irqsave(&chip->voice_lock, flags);
	pvoice->use = pvoice->pcm = pvoice->synth = pvoice->midi = 0;
	pvoice->ypcm = NULL;
	pvoice->interrupt = NULL;
	spin_unlock_irqrestore(&chip->voice_lock, flags);
	return 0;
}

/*
 *  PCM part
 */

static void snd_ymfpci_pcm_interrupt(ymfpci_t *chip, ymfpci_voice_t *voice)
{
	ymfpci_pcm_t *ypcm;
	u32 pos, delta;
	
	if ((ypcm = voice->ypcm) == NULL)
		return;
	if (ypcm->substream == NULL)
		return;
	spin_lock(&chip->reg_lock);
	if (ypcm->running) {
		pos = le32_to_cpu(voice->bank[chip->active_bank].start);
		if (pos < ypcm->last_pos)
			delta = pos + (ypcm->buffer_size - ypcm->last_pos);
		else
			delta = pos - ypcm->last_pos;
		ypcm->period_pos += delta;
		ypcm->last_pos = pos;
		while (ypcm->period_pos >= ypcm->period_size) {
			ypcm->period_pos = 0;
			// printk("done - active_bank = 0x%x, start = 0x%x\n", chip->active_bank, voice->bank[chip->active_bank].start);
			spin_unlock(&chip->reg_lock);
			snd_pcm_period_elapsed(ypcm->substream);
			spin_lock(&chip->reg_lock);
		}
	}
	spin_unlock(&chip->reg_lock);
}

static void snd_ymfpci_pcm_capture_interrupt(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return);
	ymfpci_t *chip = ypcm->chip;
	u32 pos, delta;
	
	spin_lock(&chip->reg_lock);
	if (ypcm->running) {
		pos = le32_to_cpu(chip->bank_capture[ypcm->capture_bank_number][chip->active_bank]->start) >> ypcm->shift;
		if (pos < ypcm->last_pos)
			delta = pos + (ypcm->buffer_size - ypcm->last_pos);
		else
			delta = pos - ypcm->last_pos;
		ypcm->period_pos += delta;
		ypcm->last_pos = pos;
		while (ypcm->period_pos >= ypcm->period_size) {
			ypcm->period_pos = 0;
			// printk("done - active_bank = 0x%x, start = 0x%x\n", chip->active_bank, voice->bank[chip->active_bank].start);
			spin_unlock(&chip->reg_lock);
			snd_pcm_period_elapsed(substream);
			spin_lock(&chip->reg_lock);
		}
	}
	spin_unlock(&chip->reg_lock);
}

static int snd_ymfpci_playback_trigger(snd_pcm_substream_t * substream,
				       int cmd)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, substream->runtime->private_data, return -ENXIO);
	int result = 0;

	spin_lock(&chip->reg_lock);
	if (ypcm->voices[0] == NULL) {
		result = -EINVAL;
	} else if (cmd == SNDRV_PCM_TRIGGER_START) {
		chip->ctrl_playback[ypcm->voices[0]->number + 1] = cpu_to_le32(ypcm->voices[0]->bank_addr);
		if (ypcm->voices[1] != NULL)
			chip->ctrl_playback[ypcm->voices[1]->number + 1] = cpu_to_le32(ypcm->voices[1]->bank_addr);
		ypcm->running = 1;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		chip->ctrl_playback[ypcm->voices[0]->number + 1] = 0;
		if (ypcm->voices[1] != NULL)
			chip->ctrl_playback[ypcm->voices[1]->number + 1] = 0;
		ypcm->running = 0;
	} else {
		result = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

static int snd_ymfpci_capture_trigger(snd_pcm_substream_t * substream,
				      int cmd)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, substream->runtime->private_data, return -ENXIO);
	int result = 0;
	u32 tmp;

	spin_lock(&chip->reg_lock);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		tmp = snd_ymfpci_readl(chip, YDSXGR_MAPOFREC) | (1 << ypcm->capture_bank_number);
		snd_ymfpci_writel(chip, YDSXGR_MAPOFREC, tmp);
		ypcm->running = 1;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		tmp = snd_ymfpci_readl(chip, YDSXGR_MAPOFREC) & ~(1 << ypcm->capture_bank_number);
		snd_ymfpci_writel(chip, YDSXGR_MAPOFREC, tmp);
		ypcm->running = 0;
	} else {
		result = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

static int snd_ymfpci_pcm_voice_alloc(ymfpci_pcm_t *ypcm, int voices)
{
	int err;

	if (ypcm->voices[1] != NULL && voices < 2) {
		snd_ymfpci_voice_free(ypcm->chip, ypcm->voices[1]);
		ypcm->voices[1] = NULL;
	}
	if (voices == 1 && ypcm->voices[0] != NULL)
		return 0;		/* already allocated */
	if (voices == 2 && ypcm->voices[0] != NULL && ypcm->voices[1] != NULL)
		return 0;		/* already allocated */
	if (voices > 1) {
		if (ypcm->voices[0] != NULL && ypcm->voices[1] == NULL) {
			snd_ymfpci_voice_free(ypcm->chip, ypcm->voices[0]);
			ypcm->voices[0] = NULL;
		}		
	}
	err = snd_ymfpci_voice_alloc(ypcm->chip, YMFPCI_PCM, voices > 1, &ypcm->voices[0]);
	if (err < 0)
		return err;
	ypcm->voices[0]->ypcm = ypcm;
	ypcm->voices[0]->interrupt = snd_ymfpci_pcm_interrupt;
	if (voices > 1) {
		ypcm->voices[1] = &ypcm->chip->voices[ypcm->voices[0]->number + 1];
		ypcm->voices[1]->ypcm = ypcm;
	}
	return 0;
}

static void snd_ymfpci_pcm_init_voice(ymfpci_voice_t *voice, int stereo,
				      int rate, int w_16, unsigned long addr,
				      unsigned int end, int eff2)
{
	u32 format;
	u32 delta = snd_ymfpci_calc_delta(rate);
	u32 lpfQ = snd_ymfpci_calc_lpfQ(rate);
	u32 lpfK = snd_ymfpci_calc_lpfK(rate);
	snd_ymfpci_playback_bank_t *bank;
	unsigned int nbank;

	snd_assert(voice != NULL, return);
	format = (stereo ? 0x00010000 : 0) | (w_16 ? 0 : 0x80000000);
	for (nbank = 0; nbank < 2; nbank++) {
		bank = &voice->bank[nbank];
		bank->format = cpu_to_le32(format);
		bank->loop_default = 0;
		bank->base = cpu_to_le32(addr);
		bank->loop_start = 0;
		bank->loop_end = cpu_to_le32(end);
		bank->loop_frac = 0;
		bank->eg_gain_end = cpu_to_le32(0x40000000);
		bank->lpfQ = cpu_to_le32(lpfQ);
		bank->status = 0;
		bank->num_of_frames = 0;
		bank->loop_count = 0;
		bank->start = 0;
		bank->start_frac = 0;
		bank->delta =
		bank->delta_end = cpu_to_le32(delta);
		bank->lpfK =
		bank->lpfK_end = cpu_to_le32(lpfK);
		bank->eg_gain = cpu_to_le32(0x40000000);
		bank->lpfD1 =
		bank->lpfD2 = 0;

		bank->left_gain = 
		bank->right_gain =
		bank->left_gain_end =
		bank->right_gain_end =
		bank->eff1_gain =
		bank->eff2_gain =
		bank->eff3_gain =
		bank->eff1_gain_end =
		bank->eff2_gain_end =
		bank->eff3_gain_end = 0;

		if (!stereo) {
			if (!eff2) {
				bank->left_gain = 
				bank->right_gain =
				bank->left_gain_end =
				bank->right_gain_end = cpu_to_le32(0x40000000);
			} else {
				bank->eff2_gain =
				bank->eff2_gain_end =
				bank->eff3_gain =
				bank->eff3_gain_end = cpu_to_le32(0x40000000);
			}
		} else {
			if (!eff2) {
				if ((voice->number & 1) == 0) {
					bank->left_gain =
					bank->left_gain_end = cpu_to_le32(0x40000000);
				} else {
					bank->format |= cpu_to_le32(1);
					bank->right_gain =
					bank->right_gain_end = cpu_to_le32(0x40000000);
				}
			} else {
				if ((voice->number & 1) == 0) {
					bank->eff2_gain =
					bank->eff2_gain_end = cpu_to_le32(0x40000000);
				} else {
					bank->format |= cpu_to_le32(1);
					bank->eff3_gain =
					bank->eff3_gain_end = cpu_to_le32(0x40000000);
				}
			}
		}
	}
}

static int snd_ymfpci_ac3_init(ymfpci_t *chip)
{
	unsigned char *ptr;
	dma_addr_t ptr_addr;

	if (chip->ac3_tmp_base != NULL)
		return -EBUSY;
	if ((ptr = snd_malloc_pci_pages(chip->pci, 4096, &ptr_addr)) == NULL)
		return -ENOMEM;

	chip->ac3_tmp_base = ptr;
	chip->ac3_tmp_base_addr = ptr_addr;
	chip->bank_effect[3][0]->base =
	chip->bank_effect[3][1]->base = cpu_to_le32(chip->ac3_tmp_base_addr);
	chip->bank_effect[3][0]->loop_end =
	chip->bank_effect[3][1]->loop_end = cpu_to_le32(1024);
	chip->bank_effect[4][0]->base =
	chip->bank_effect[4][1]->base = cpu_to_le32(chip->ac3_tmp_base_addr + 2048);
	chip->bank_effect[4][0]->loop_end =
	chip->bank_effect[4][1]->loop_end = cpu_to_le32(1024);

	spin_lock_irq(&chip->reg_lock);
	snd_ymfpci_writel(chip, YDSXGR_MAPOFEFFECT,
			  snd_ymfpci_readl(chip, YDSXGR_MAPOFEFFECT) | 3 << 3);
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int snd_ymfpci_ac3_done(ymfpci_t *chip)
{
	spin_lock_irq(&chip->reg_lock);
	snd_ymfpci_writel(chip, YDSXGR_MAPOFEFFECT,
			  snd_ymfpci_readl(chip, YDSXGR_MAPOFEFFECT) & ~(3 << 3));
	spin_unlock_irq(&chip->reg_lock);
	snd_ymfpci_irq_wait(chip);
	if (chip->ac3_tmp_base) {
		snd_free_pci_pages(chip->pci, 4096, chip->ac3_tmp_base, chip->ac3_tmp_base_addr);
		chip->ac3_tmp_base = NULL;
	}
	return 0;
}

static int snd_ymfpci_playback_hw_params(snd_pcm_substream_t * substream,
					 snd_pcm_hw_params_t * hw_params)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return -ENXIO);
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	if ((err = snd_ymfpci_pcm_voice_alloc(ypcm, params_channels(hw_params))) < 0)
		return err;
	if (ypcm->spdif || ypcm->mode4ch)
		if ((err = snd_ymfpci_ac3_init(chip)) < 0)
			return err;
	return 0;
}

static int snd_ymfpci_playback_hw_free(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return -ENXIO);

	/* wait, until the PCI operations are not finished */
	snd_ymfpci_irq_wait(chip);
	snd_pcm_lib_free_pages(substream);
	if (ypcm->voices[1]) {
		snd_ymfpci_voice_free(chip, ypcm->voices[1]);
		ypcm->voices[1] = NULL;
	}
	if (ypcm->voices[0]) {
		snd_ymfpci_voice_free(chip, ypcm->voices[0]);
		ypcm->voices[0] = NULL;
	}
	if (ypcm->spdif || ypcm->mode4ch)
		snd_ymfpci_ac3_done(chip);
	return 0;
}

static int snd_ymfpci_playback_prepare(snd_pcm_substream_t * substream)
{
	// ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return -ENXIO);
	int nvoice;

	ypcm->period_size = runtime->period_size;
	ypcm->buffer_size = runtime->buffer_size;
	ypcm->period_pos = 0;
	ypcm->last_pos = 0;
	for (nvoice = 0; nvoice < runtime->channels; nvoice++)
		snd_ymfpci_pcm_init_voice(ypcm->voices[nvoice],
					  runtime->channels == 2,
					  runtime->rate,
					  snd_pcm_format_width(runtime->format) == 16,
					  runtime->dma_addr,
					  ypcm->buffer_size,
					  ypcm->spdif || ypcm->mode4ch);
	return 0;
}

static int snd_ymfpci_capture_hw_params(snd_pcm_substream_t * substream,
					snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_ymfpci_capture_hw_free(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);

	/* wait, until the PCI operations are not finished */
	snd_ymfpci_irq_wait(chip);
	return snd_pcm_lib_free_pages(substream);
}

static int snd_ymfpci_capture_prepare(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return -ENXIO);
	snd_ymfpci_capture_bank_t * bank;
	int nbank;
	u32 rate, format;

	ypcm->period_size = runtime->period_size;
	ypcm->buffer_size = runtime->buffer_size;
	ypcm->period_pos = 0;
	ypcm->last_pos = 0;
	ypcm->shift = 0;
	rate = ((48000 * 4096) / runtime->rate) - 1;
	format = 0;
	if (runtime->channels == 2) {
		format |= 2;
		ypcm->shift++;
	}
	if (snd_pcm_format_width(runtime->format) == 8)
		format |= 1;
	else
		ypcm->shift++;
	switch (ypcm->capture_bank_number) {
	case 0:
		snd_ymfpci_writel(chip, YDSXGR_RECFORMAT, format);
		snd_ymfpci_writel(chip, YDSXGR_RECSLOTSR, rate);
		break;
	case 1:
		snd_ymfpci_writel(chip, YDSXGR_ADCFORMAT, format);
		snd_ymfpci_writel(chip, YDSXGR_ADCSLOTSR, rate);
		break;
	}
	for (nbank = 0; nbank < 2; nbank++) {
		bank = chip->bank_capture[ypcm->capture_bank_number][nbank];
		bank->base = cpu_to_le32(runtime->dma_addr);
		bank->loop_end = cpu_to_le32(ypcm->buffer_size << ypcm->shift);
		bank->start = 0;
		bank->num_of_loops = 0;
	}
	return 0;
}

static snd_pcm_uframes_t snd_ymfpci_playback_pointer(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return -ENXIO);
	ymfpci_voice_t *voice = ypcm->voices[0];

	if (!(ypcm->running && voice))
		return 0;
	return le32_to_cpu(voice->bank[chip->active_bank].start);
}

static snd_pcm_uframes_t snd_ymfpci_capture_pointer(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return -ENXIO);

	if (!ypcm->running)
		return 0;
	return le32_to_cpu(chip->bank_capture[ypcm->capture_bank_number][chip->active_bank]->start) >> ypcm->shift;
}

static void snd_ymfpci_irq_wait(ymfpci_t *chip)
{
	wait_queue_t wait;
	int loops = 4;

	while (loops-- > 0) {
		if ((snd_ymfpci_readl(chip, YDSXGR_MODE) & 3) == 0)
		 	continue;
		init_waitqueue_entry(&wait, current);
		add_wait_queue(&chip->interrupt_sleep, &wait);
		atomic_inc(&chip->interrupt_sleep_count);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ/20);
		remove_wait_queue(&chip->interrupt_sleep, &wait);
	}
}

static void snd_ymfpci_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, dev_id, return);
	u32 status, nvoice, mode;
	ymfpci_voice_t *voice;

	status = snd_ymfpci_readl(chip, YDSXGR_STATUS);
	if (status & 0x80000000) {
		chip->active_bank = snd_ymfpci_readl(chip, YDSXGR_CTRLSELECT) & 1;
		spin_lock(&chip->voice_lock);
		for (nvoice = 0; nvoice < YDSXG_PLAYBACK_VOICES; nvoice++) {
			voice = &chip->voices[nvoice];
			if (voice->interrupt)
				voice->interrupt(chip, voice);
		}
		for (nvoice = 0; nvoice < YDSXG_CAPTURE_VOICES; nvoice++) {
			if (chip->capture_substream[nvoice])
				snd_ymfpci_pcm_capture_interrupt(chip->capture_substream[nvoice]);
		}
#if 0
		for (nvoice = 0; nvoice < YDSXG_EFFECT_VOICES; nvoice++) {
			if (chip->effect_substream[nvoice])
				snd_ymfpci_pcm_effect_interrupt(chip->effect_substream[nvoice]);
		}
#endif
		spin_unlock(&chip->voice_lock);
		spin_lock(&chip->reg_lock);
		snd_ymfpci_writel(chip, YDSXGR_STATUS, 0x80000000);
		mode = snd_ymfpci_readl(chip, YDSXGR_MODE) | 2;
		snd_ymfpci_writel(chip, YDSXGR_MODE, mode);
		spin_unlock(&chip->reg_lock);

		if (atomic_read(&chip->interrupt_sleep_count)) {
			atomic_set(&chip->interrupt_sleep_count, 0);
			wake_up(&chip->interrupt_sleep);
		}
	}

	status = snd_ymfpci_readl(chip, YDSXGR_INTFLAG);
	if (status & 1) {
		/* timer handler */
		snd_ymfpci_writel(chip, YDSXGR_INTFLAG, ~0);
	}
}

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_ymfpci_playback =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	8000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	256 * 1024, /* FIXME: enough? */
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	256 * 1024, /* FIXME: enough? */
/*	periods_min:	  */	3,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

static snd_pcm_hardware_t snd_ymfpci_capture =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	8000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	256 * 1024, /* FIXME: enough? */
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	256 * 1024, /* FIXME: enough? */
/*	periods_min:	  */	3,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};
#else
static snd_pcm_hardware_t snd_ymfpci_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		8000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	256 * 1024, /* FIXME: enough? */
	period_bytes_min:	64,
	period_bytes_max:	256 * 1024, /* FIXME: enough? */
	periods_min:		3,
	periods_max:		1024,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_ymfpci_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		8000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	256 * 1024, /* FIXME: enough? */
	period_bytes_min:	64,
	period_bytes_max:	256 * 1024, /* FIXME: enough? */
	periods_min:		3,
	periods_max:		1024,
	fifo_size:		0,
};
#endif

static void snd_ymfpci_pcm_free_substream(snd_pcm_runtime_t *runtime)
{
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return);
	
	if (ypcm)
		snd_magic_kfree(ypcm);
}

static int snd_ymfpci_playback_open(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm;

	ypcm = snd_magic_kcalloc(ymfpci_pcm_t, 0, GFP_KERNEL);
	if (ypcm == NULL)
		return -ENOMEM;
	ypcm->chip = chip;
	ypcm->type = PLAYBACK_VOICE;
	ypcm->substream = substream;
	runtime->hw = snd_ymfpci_playback;
	runtime->private_data = ypcm;
	runtime->private_free = snd_ymfpci_pcm_free_substream;
	/* FIXME? True value is 256/48 = 5.33333 ms */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 5334, UINT_MAX);
	return 0;
}

static int snd_ymfpci_playback_spdif_open(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm;
	unsigned long flags;
	int err;
	
	if ((err = snd_ymfpci_playback_open(substream)) < 0)
		return err;
	ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return 0);
	ypcm->spdif = 1;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTCTRL,
			  snd_ymfpci_readw(chip, YDSXGR_SPDIFOUTCTRL) | 2);
	snd_ymfpci_writel(chip, YDSXGR_MODE,
			  snd_ymfpci_readl(chip, YDSXGR_MODE) | (1 << 30));
	chip->spdif_pcm_bits = chip->spdif_bits;
	snd_ymfpci_writel(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_pcm_bits);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	chip->spdif_pcm_ctl->access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &chip->spdif_pcm_ctl->id);

	/* FIXME? True value is 256/48 = 5.33333 ms */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 5334, UINT_MAX);
	return 0;
}

static int snd_ymfpci_playback_4ch_open(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm;
	unsigned long flags;
	int err;
	
	if ((err = snd_ymfpci_playback_open(substream)) < 0)
		return err;
	ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return 0);
	ypcm->mode4ch = 1;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ymfpci_writew(chip, YDSXGR_SECCONFIG,
			  (snd_ymfpci_readw(chip, YDSXGR_SECCONFIG) & ~0x0030) | 0x0010);
	snd_ymfpci_writel(chip, YDSXGR_MODE,
			  snd_ymfpci_readl(chip, YDSXGR_MODE) | (1 << 30));
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	/* FIXME? True value is 256/48 = 5.33333 ms */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 5334, UINT_MAX);
	return 0;
}

static int snd_ymfpci_capture_open(snd_pcm_substream_t * substream,
				   u32 capture_bank_number)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm;

	ypcm = snd_magic_kcalloc(ymfpci_pcm_t, 0, GFP_KERNEL);
	if (ypcm == NULL)
		return -ENOMEM;
	ypcm->chip = chip;
	ypcm->type = capture_bank_number + CAPTURE_REC;
	ypcm->substream = substream;	
	ypcm->capture_bank_number = capture_bank_number;
	chip->capture_substream[capture_bank_number] = substream;
	runtime->hw = snd_ymfpci_capture;
	/* FIXME? True value is 256/48 = 5.33333 ms */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 5334, UINT_MAX);
	runtime->private_data = ypcm;
	runtime->private_free = snd_ymfpci_pcm_free_substream;
	snd_ymfpci_hw_start(chip);
	return 0;
}

static int snd_ymfpci_capture_rec_open(snd_pcm_substream_t * substream)
{
	return snd_ymfpci_capture_open(substream, 0);
}

static int snd_ymfpci_capture_ac97_open(snd_pcm_substream_t * substream)
{
	return snd_ymfpci_capture_open(substream, 1);
}

static int snd_ymfpci_playback_close(snd_pcm_substream_t * substream)
{
	return 0;
}

static int snd_ymfpci_playback_spdif_close(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ymfpci_writel(chip, YDSXGR_MODE,
			  snd_ymfpci_readl(chip, YDSXGR_MODE) & ~(1 << 30));
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTCTRL,
			  snd_ymfpci_readw(chip, YDSXGR_SPDIFOUTCTRL) & ~2);
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_bits);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	chip->spdif_pcm_ctl->access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &chip->spdif_pcm_ctl->id);
	return snd_ymfpci_playback_close(substream);
}

static int snd_ymfpci_playback_4ch_close(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ymfpci_writel(chip, YDSXGR_MODE,
			  snd_ymfpci_readl(chip, YDSXGR_MODE) & ~(1 << 30));
	snd_ymfpci_writew(chip, YDSXGR_SECCONFIG,
			  (snd_ymfpci_readw(chip, YDSXGR_SECCONFIG) & ~0x0330) | 0x0010);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return snd_ymfpci_playback_close(substream);
}

static int snd_ymfpci_capture_close(snd_pcm_substream_t * substream)
{
	ymfpci_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, return -ENXIO);

	if (ypcm != NULL) {
		chip->capture_substream[ypcm->capture_bank_number] = NULL;
		snd_ymfpci_hw_stop(chip);
	}
	return 0;
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_ymfpci_playback_ops = {
	snd_ymfpci_playback_open,
	snd_ymfpci_playback_close,
	snd_pcm_lib_ioctl,
	snd_ymfpci_playback_hw_params,
	snd_ymfpci_playback_hw_free,
	snd_ymfpci_playback_prepare,
	snd_ymfpci_playback_trigger,
	snd_ymfpci_playback_pointer,0,0
};

static snd_pcm_ops_t snd_ymfpci_capture_rec_ops = {
	snd_ymfpci_capture_rec_open,
	snd_ymfpci_capture_close,
	snd_pcm_lib_ioctl,
	snd_ymfpci_capture_hw_params,
	snd_ymfpci_capture_hw_free,
	snd_ymfpci_capture_prepare,
	snd_ymfpci_capture_trigger,
	snd_ymfpci_capture_pointer,0,0
};
#else
static snd_pcm_ops_t snd_ymfpci_playback_ops = {
	open:			snd_ymfpci_playback_open,
	close:			snd_ymfpci_playback_close,
	ioctl:			snd_pcm_lib_ioctl,
	hw_params:		snd_ymfpci_playback_hw_params,
	hw_free:		snd_ymfpci_playback_hw_free,
	prepare:		snd_ymfpci_playback_prepare,
	trigger:		snd_ymfpci_playback_trigger,
	pointer:		snd_ymfpci_playback_pointer,
};

static snd_pcm_ops_t snd_ymfpci_capture_rec_ops = {
	open:			snd_ymfpci_capture_rec_open,
	close:			snd_ymfpci_capture_close,
	ioctl:			snd_pcm_lib_ioctl,
	hw_params:		snd_ymfpci_capture_hw_params,
	hw_free:		snd_ymfpci_capture_hw_free,
	prepare:		snd_ymfpci_capture_prepare,
	trigger:		snd_ymfpci_capture_trigger,
	pointer:		snd_ymfpci_capture_pointer,
};
#endif

static void snd_ymfpci_pcm_free(snd_pcm_t *pcm)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, pcm->private_data, return);
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int snd_ymfpci_pcm(ymfpci_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(chip->card, "YMFPCI", device, 32, 1, &pcm)) < 0)
		return err;
	pcm->private_data = chip;
	pcm->private_free = snd_ymfpci_pcm_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ymfpci_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ymfpci_capture_rec_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "YMFPCI");
	chip->pcm = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024, 256*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_ymfpci_capture_ac97_ops = {
	snd_ymfpci_capture_ac97_open,
	snd_ymfpci_capture_close,
	snd_pcm_lib_ioctl,
	snd_ymfpci_capture_hw_params,
	snd_ymfpci_capture_hw_free,
	snd_ymfpci_capture_prepare,
	snd_ymfpci_capture_trigger,
	snd_ymfpci_capture_pointer,0,0
};
#else
static snd_pcm_ops_t snd_ymfpci_capture_ac97_ops = {
	open:			snd_ymfpci_capture_ac97_open,
	close:			snd_ymfpci_capture_close,
	ioctl:			snd_pcm_lib_ioctl,
	hw_params:		snd_ymfpci_capture_hw_params,
	hw_free:		snd_ymfpci_capture_hw_free,
	prepare:		snd_ymfpci_capture_prepare,
	trigger:		snd_ymfpci_capture_trigger,
	pointer:		snd_ymfpci_capture_pointer,
};
#endif

static void snd_ymfpci_pcm2_free(snd_pcm_t *pcm)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, pcm->private_data, return);
	chip->pcm2 = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int snd_ymfpci_pcm2(ymfpci_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(chip->card, "YMFPCI - AC'97", device, 0, 1, &pcm)) < 0)
		return err;
	pcm->private_data = chip;
	pcm->private_free = snd_ymfpci_pcm2_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ymfpci_capture_ac97_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "YMFPCI - AC'97");
	chip->pcm2 = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024, 256*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_ymfpci_playback_spdif_ops = {
	snd_ymfpci_playback_spdif_open,
	snd_ymfpci_playback_spdif_close,
	snd_pcm_lib_ioctl,
	snd_ymfpci_playback_hw_params,
	snd_ymfpci_playback_hw_free,
	snd_ymfpci_playback_prepare,
	snd_ymfpci_playback_trigger,
	snd_ymfpci_playback_pointer,0,0
#else
static snd_pcm_ops_t snd_ymfpci_playback_spdif_ops = {
	open:			snd_ymfpci_playback_spdif_open,
	close:			snd_ymfpci_playback_spdif_close,
	ioctl:			snd_pcm_lib_ioctl,
	hw_params:		snd_ymfpci_playback_hw_params,
	hw_free:		snd_ymfpci_playback_hw_free,
	prepare:		snd_ymfpci_playback_prepare,
	trigger:		snd_ymfpci_playback_trigger,
	pointer:		snd_ymfpci_playback_pointer,
#endif
};

static void snd_ymfpci_pcm_spdif_free(snd_pcm_t *pcm)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, pcm->private_data, return);
	chip->pcm_spdif = NULL;
}

int snd_ymfpci_pcm_spdif(ymfpci_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(chip->card, "YMFPCI - IEC958", device, 1, 0, &pcm)) < 0)
		return err;
	pcm->private_data = chip;
	pcm->private_free = snd_ymfpci_pcm_spdif_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ymfpci_playback_spdif_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "YMFPCI - IEC958");
	chip->pcm_spdif = pcm;
	if (rpcm)
		*rpcm = pcm;
	return 0;
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_ymfpci_playback_4ch_ops = {
	snd_ymfpci_playback_4ch_open,
	snd_ymfpci_playback_4ch_close,
	snd_pcm_lib_ioctl,
	snd_ymfpci_playback_hw_params,
	snd_ymfpci_playback_hw_free,
	snd_ymfpci_playback_prepare,
	snd_ymfpci_playback_trigger,
	snd_ymfpci_playback_pointer,0,0
};
#else
static snd_pcm_ops_t snd_ymfpci_playback_4ch_ops = {
	open:			snd_ymfpci_playback_4ch_open,
	close:			snd_ymfpci_playback_4ch_close,
	ioctl:			snd_pcm_lib_ioctl,
	hw_params:		snd_ymfpci_playback_hw_params,
	hw_free:		snd_ymfpci_playback_hw_free,
	prepare:		snd_ymfpci_playback_prepare,
	trigger:		snd_ymfpci_playback_trigger,
	pointer:		snd_ymfpci_playback_pointer,
};
#endif

static void snd_ymfpci_pcm_4ch_free(snd_pcm_t *pcm)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, pcm->private_data, return);
	chip->pcm_4ch = NULL;
}

int snd_ymfpci_pcm_4ch(ymfpci_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(chip->card, "YMFPCI - Rear", device, 1, 0, &pcm)) < 0)
		return err;
	pcm->private_data = chip;
	pcm->private_free = snd_ymfpci_pcm_4ch_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ymfpci_playback_4ch_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "YMFPCI - Rear PCM");
	chip->pcm_4ch = pcm;
	if (rpcm)
		*rpcm = pcm;
	return 0;
}

static int snd_ymfpci_spdif_default_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ymfpci_spdif_default_get(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ymfpci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.iec958.status[0] = (chip->spdif_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (chip->spdif_bits >> 8) & 0xff;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_ymfpci_spdif_default_put(snd_kcontrol_t * kcontrol,
					 snd_ctl_elem_value_t * ucontrol)
{
	ymfpci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;

	val = ((ucontrol->value.iec958.status[0] & 0x3e) << 0) |
	      (ucontrol->value.iec958.status[1] << 8);
	spin_lock_irqsave(&chip->reg_lock, flags);
	change = chip->spdif_bits != val;
	chip->spdif_bits = val;
	if ((snd_ymfpci_readw(chip, YDSXGR_SPDIFOUTCTRL) & 1) && chip->pcm_spdif == NULL)
		snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_bits);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_ymfpci_spdif_default =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),0,0,
	snd_ymfpci_spdif_default_info,
	snd_ymfpci_spdif_default_get,
	snd_ymfpci_spdif_default_put,0
#else
	iface:		SNDRV_CTL_ELEM_IFACE_PCM,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	info:		snd_ymfpci_spdif_default_info,
	get:		snd_ymfpci_spdif_default_get,
	put:		snd_ymfpci_spdif_default_put
#endif
};

static int snd_ymfpci_spdif_mask_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ymfpci_spdif_mask_get(snd_kcontrol_t * kcontrol,
				      snd_ctl_elem_value_t * ucontrol)
{
	ymfpci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.iec958.status[0] = 0x3e;
	ucontrol->value.iec958.status[1] = 0xff;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static snd_kcontrol_new_t snd_ymfpci_spdif_mask =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),0,
	SNDRV_CTL_ELEM_ACCESS_READ,
	snd_ymfpci_spdif_mask_info,
	snd_ymfpci_spdif_mask_get,0,0
#else
	access:		SNDRV_CTL_ELEM_ACCESS_READ,
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	info:		snd_ymfpci_spdif_mask_info,
	get:		snd_ymfpci_spdif_mask_get,
#endif
};

static int snd_ymfpci_spdif_stream_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ymfpci_spdif_stream_get(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ymfpci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	ucontrol->value.iec958.status[0] = (chip->spdif_pcm_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (chip->spdif_pcm_bits >> 8) & 0xff;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_ymfpci_spdif_stream_put(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ymfpci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;

	val = ((ucontrol->value.iec958.status[0] & 0x3e) << 0) |
	      (ucontrol->value.iec958.status[1] << 8);
	spin_lock_irqsave(&chip->reg_lock, flags);
	change = chip->spdif_pcm_bits != val;
	chip->spdif_pcm_bits = val;
	if ((snd_ymfpci_readw(chip, YDSXGR_SPDIFOUTCTRL) & 2))
		snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_pcm_bits);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_ymfpci_spdif_stream =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),0,
	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	snd_ymfpci_spdif_stream_info,
	snd_ymfpci_spdif_stream_get,
	snd_ymfpci_spdif_stream_put,0
#else
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	iface:		SNDRV_CTL_ELEM_IFACE_PCM,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	info:		snd_ymfpci_spdif_stream_info,
	get:		snd_ymfpci_spdif_stream_get,
	put:		snd_ymfpci_spdif_stream_put
#endif
};

/*
 *  Mixer controls
 */

#ifdef TARGET_OS2
#define YMFPCI_SINGLE(xname, xindex, reg) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0,0, xname, xindex, \
  0, snd_ymfpci_info_single, \
  snd_ymfpci_get_single, snd_ymfpci_put_single, \
  reg }
#else
#define YMFPCI_SINGLE(xname, xindex, reg) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_ymfpci_info_single, \
  get: snd_ymfpci_get_single, put: snd_ymfpci_put_single, \
  private_value: reg }
#endif

static int snd_ymfpci_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	unsigned int mask = 1;

	switch (kcontrol->private_value) {
	case YDSXGR_SPDIFOUTCTRL: break;
	case YDSXGR_SPDIFINCTRL: break;
	default: return -EINVAL;
	}
	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ymfpci_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ymfpci_t *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value;
	unsigned int shift = 0, mask = 1, invert = 0;
	
	switch (kcontrol->private_value) {
	case YDSXGR_SPDIFOUTCTRL: break;
	case YDSXGR_SPDIFINCTRL: break;
	default: return -EINVAL;
	}
	ucontrol->value.integer.value[0] = (snd_ymfpci_readl(chip, reg) >> shift) & mask;
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

static int snd_ymfpci_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ymfpci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value;
	unsigned int shift = 0, mask = 1, invert = 0;
	int change;
	unsigned int val, oval;
	
	switch (kcontrol->private_value) {
	case YDSXGR_SPDIFOUTCTRL: break;
	case YDSXGR_SPDIFINCTRL: break;
	default: return -EINVAL;
	}
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	val <<= shift;
	spin_lock_irqsave(&chip->reg_lock, flags);
	oval = snd_ymfpci_readl(chip, reg);
	val = (oval & ~(mask << shift)) | val;
	change = val != oval;
	snd_ymfpci_writel(chip, reg, val);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

#ifdef TARGET_OS2
#define YMFPCI_DOUBLE(xname, xindex, reg) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0,0, xname, xindex, \
  0, snd_ymfpci_info_double, \
  snd_ymfpci_get_double, snd_ymfpci_put_double, \
  reg }
#else
#define YMFPCI_DOUBLE(xname, xindex, reg) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_ymfpci_info_double, \
  get: snd_ymfpci_get_double, put: snd_ymfpci_put_double, \
  private_value: reg }
#endif

static int snd_ymfpci_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	unsigned int reg = kcontrol->private_value;
	unsigned int mask = 16383;

	if (reg < 0x80 || reg >= 0xc0)
		return -EINVAL;
	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ymfpci_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ymfpci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int reg = kcontrol->private_value;
	unsigned int shift_left = 0, shift_right = 16, mask = 16383, invert = 0;
	unsigned int val;
	
	if (reg < 0x80 || reg >= 0xc0)
		return -EINVAL;
	spin_lock_irqsave(&chip->reg_lock, flags);
	val = snd_ymfpci_readl(chip, reg);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	ucontrol->value.integer.value[0] = (val >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (val >> shift_right) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

static int snd_ymfpci_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ymfpci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int reg = kcontrol->private_value;
	unsigned int shift_left = 0, shift_right = 16, mask = 16383, invert = 0;
	int change;
	unsigned int val1, val2, oval;
	
	if (reg < 0x80 || reg >= 0xc0)
		return -EINVAL;
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;
	spin_lock_irqsave(&chip->reg_lock, flags);
	oval = snd_ymfpci_readl(chip, reg);
	val1 = (oval & ~((mask << shift_left) | (mask << shift_right))) | val1 | val2;
	change = val1 != oval;
	snd_ymfpci_writel(chip, reg, val1);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

#define YMFPCI_CONTROLS (sizeof(snd_ymfpci_controls)/sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_ymfpci_controls[] = {
YMFPCI_DOUBLE("Wave Playback Volume", 0, YDSXGR_NATIVEDACOUTVOL),
YMFPCI_DOUBLE("Wave Capture Volume", 0, YDSXGR_NATIVEDACLOOPVOL),
YMFPCI_DOUBLE("Digital Capture Volume", 0, YDSXGR_NATIVEDACINVOL),
YMFPCI_DOUBLE("Digital Capture Volume", 1, YDSXGR_NATIVEADCINVOL),
YMFPCI_DOUBLE("ADC Playback Volume", 0, YDSXGR_PRIADCOUTVOL),
YMFPCI_DOUBLE("ADC Capture Volume", 0, YDSXGR_PRIADCLOOPVOL),
YMFPCI_DOUBLE("ADC Playback Volume", 1, YDSXGR_SECADCOUTVOL),
YMFPCI_DOUBLE("ADC Capture Volume", 1, YDSXGR_SECADCLOOPVOL),
YMFPCI_DOUBLE(SNDRV_CTL_NAME_IEC958("AC97 ", PLAYBACK,VOLUME), 0, YDSXGR_ZVOUTVOL),
YMFPCI_DOUBLE(SNDRV_CTL_NAME_IEC958("", CAPTURE,VOLUME), 0, YDSXGR_ZVLOOPVOL),
YMFPCI_DOUBLE(SNDRV_CTL_NAME_IEC958("AC97 ",PLAYBACK,VOLUME), 1, YDSXGR_SPDIFOUTVOL),
YMFPCI_DOUBLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,VOLUME), 1, YDSXGR_SPDIFLOOPVOL),
YMFPCI_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH), 0, YDSXGR_SPDIFOUTCTRL),
YMFPCI_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), 0, YDSXGR_SPDIFINCTRL)
};

/*
 *  Mixer routines
 */

static void snd_ymfpci_mixer_free_ac97(ac97_t *ac97)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, ac97->private_data, return);
	chip->ac97 = NULL;
}

int snd_ymfpci_mixer(ymfpci_t *chip)
{
	ac97_t ac97;
	snd_kcontrol_t *kctl;
	int err, idx;

	memset(&ac97, 0, sizeof(ac97));
	ac97.write = snd_ymfpci_codec_write;
	ac97.read = snd_ymfpci_codec_read;
	ac97.private_data = chip;
	ac97.private_free = snd_ymfpci_mixer_free_ac97;
	if ((err = snd_ac97_mixer(chip->card, &ac97, &chip->ac97)) < 0)
		return err;

	for (idx = 0; idx < YMFPCI_CONTROLS; idx++) {
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_ymfpci_controls[idx], chip))) < 0)
			return err;
	}

	/* add S/PDIF control */
	snd_assert(chip->pcm_spdif != NULL, return -EIO);
	if ((err = snd_ctl_add(chip->card, kctl = snd_ctl_new1(&snd_ymfpci_spdif_default, chip))) < 0)
		return err;
	kctl->id.device = chip->pcm_spdif->device;
	if ((err = snd_ctl_add(chip->card, kctl = snd_ctl_new1(&snd_ymfpci_spdif_mask, chip))) < 0)
		return err;
	kctl->id.device = chip->pcm_spdif->device;
	if ((err = snd_ctl_add(chip->card, kctl = snd_ctl_new1(&snd_ymfpci_spdif_stream, chip))) < 0)
		return err;
	kctl->id.device = chip->pcm_spdif->device;
	chip->spdif_pcm_ctl = kctl;

	return 0;
}

/*
 *  proc interface
 */

static void snd_ymfpci_proc_read(snd_info_entry_t *entry, 
				 snd_info_buffer_t * buffer)
{
	// ymfpci_t *chip = snd_magic_cast(ymfpci_t, private_data, return);
	
	snd_iprintf(buffer, "YMFPCI\n\n");
}

static int snd_ymfpci_proc_init(snd_card_t * card, ymfpci_t *chip)
{
	snd_info_entry_t *entry;
	
	entry = snd_info_create_card_entry(card, "ymfpci", card->proc_root);
	if (entry) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = chip;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 4096;
		entry->c.text.read = snd_ymfpci_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_unregister(entry);
			entry = NULL;
		}
	}
	chip->proc_entry = entry;
	return 0;
}

static int snd_ymfpci_proc_done(ymfpci_t *chip)
{
	if (chip->proc_entry)
		snd_info_unregister((snd_info_entry_t *) chip->proc_entry);
	return 0;
}

/*
 *  initialization routines
 */

static void snd_ymfpci_aclink_reset(struct pci_dev * pci)
{
	u8 cmd;

	pci_read_config_byte(pci, PCIR_DSXGCTRL, &cmd);
#if 0 // force to reset
	if (cmd & 0x03) {
#endif
		pci_write_config_byte(pci, PCIR_DSXGCTRL, cmd & 0xfc);
		pci_write_config_byte(pci, PCIR_DSXGCTRL, cmd | 0x03);
		pci_write_config_byte(pci, PCIR_DSXGCTRL, cmd & 0xfc);
		pci_write_config_word(pci, PCIR_DSXPWRCTRL1, 0);
		pci_write_config_word(pci, PCIR_DSXPWRCTRL2, 0);
#if 0
	}
#endif
}

static void snd_ymfpci_enable_dsp(ymfpci_t *chip)
{
	snd_ymfpci_writel(chip, YDSXGR_CONFIG, 0x00000001);
}

static void snd_ymfpci_disable_dsp(ymfpci_t *chip)
{
	u32 val;
	int timeout = 1000;

	val = snd_ymfpci_readl(chip, YDSXGR_CONFIG);
	if (val)
		snd_ymfpci_writel(chip, YDSXGR_CONFIG, 0x00000000);
	while (timeout-- > 0) {
		val = snd_ymfpci_readl(chip, YDSXGR_STATUS);
		if ((val & 0x00000002) == 0)
			break;
	}
}

#include "ymfpci_image.h"

static void snd_ymfpci_download_image(ymfpci_t *chip)
{
	int i;
	u16 ctrl;
	unsigned long *inst;

	snd_ymfpci_writel(chip, YDSXGR_NATIVEDACOUTVOL, 0x00000000);
	snd_ymfpci_disable_dsp(chip);
	snd_ymfpci_writel(chip, YDSXGR_MODE, 0x00010000);
	snd_ymfpci_writel(chip, YDSXGR_MODE, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_MAPOFREC, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_MAPOFEFFECT, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_PLAYCTRLBASE, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_RECCTRLBASE, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_EFFCTRLBASE, 0x00000000);
	ctrl = snd_ymfpci_readw(chip, YDSXGR_GLOBALCTRL);
	snd_ymfpci_writew(chip, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);

	/* setup DSP instruction code */
	for (i = 0; i < YDSXG_DSPLENGTH / 4; i++)
		snd_ymfpci_writel(chip, YDSXGR_DSPINSTRAM + (i << 2), DspInst[i]);

	/* setup control instruction code */
	switch (chip->device_id) {
	case PCI_DEVICE_ID_YAMAHA_724F:
	case PCI_DEVICE_ID_YAMAHA_740C:
	case PCI_DEVICE_ID_YAMAHA_744:
	case PCI_DEVICE_ID_YAMAHA_754:
		inst = CntrlInst1E;
		break;
	default:
		inst = CntrlInst;
		break;
	}
	for (i = 0; i < YDSXG_CTRLLENGTH / 4; i++)
		snd_ymfpci_writel(chip, YDSXGR_CTRLINSTRAM + (i << 2), inst[i]);

	snd_ymfpci_enable_dsp(chip);
}

static int snd_ymfpci_memalloc(ymfpci_t *chip)
{
	long size, playback_ctrl_size;
	int voice, bank, reg;
	u8 *ptr;
	dma_addr_t ptr_addr;

	playback_ctrl_size = 4 + 4 * YDSXG_PLAYBACK_VOICES;
	chip->bank_size_playback = snd_ymfpci_readl(chip, YDSXGR_PLAYCTRLSIZE) << 2;
	chip->bank_size_capture = snd_ymfpci_readl(chip, YDSXGR_RECCTRLSIZE) << 2;
	chip->bank_size_effect = snd_ymfpci_readl(chip, YDSXGR_EFFCTRLSIZE) << 2;
	chip->work_size = YDSXG_DEFAULT_WORK_SIZE;
	
	size = ((playback_ctrl_size + 0x00ff) & ~0x00ff) +
	       ((chip->bank_size_playback * 2 * YDSXG_PLAYBACK_VOICES + 0x00ff) & ~0x00ff) +
	       ((chip->bank_size_capture * 2 * YDSXG_CAPTURE_VOICES + 0x00ff) & ~0x00ff) +
	       ((chip->bank_size_effect * 2 * YDSXG_EFFECT_VOICES + 0x00ff) & ~0x00ff) +
	       chip->work_size;
	/* work_ptr must be aligned to 256 bytes, but it's already
	   covered with the kernel page allocation mechanism */
	if ((ptr = snd_malloc_pci_pages(chip->pci, size, &ptr_addr)) == NULL)
		return -ENOMEM;
	memset(ptr, 0, size);	/* for sure */
	chip->work_ptr = ptr;
	chip->work_ptr_addr = ptr_addr;
	chip->work_ptr_size = size;

	ptr += (playback_ctrl_size + 0x00ff) & ~0x00ff;
	ptr_addr += (playback_ctrl_size + 0x00ff) & ~0x00ff;
	chip->bank_base_playback = ptr;
	chip->bank_base_playback_addr = ptr_addr;
	chip->ctrl_playback = (u32 *)ptr;
	chip->ctrl_playback[0] = cpu_to_le32(YDSXG_PLAYBACK_VOICES);
	for (voice = 0; voice < YDSXG_PLAYBACK_VOICES; voice++) {
		chip->voices[voice].number = voice;
		chip->voices[voice].bank = (snd_ymfpci_playback_bank_t *)ptr;
		chip->voices[voice].bank_addr = ptr_addr;
		for (bank = 0; bank < 2; bank++) {
			chip->bank_playback[voice][bank] = (snd_ymfpci_playback_bank_t *)ptr;
			ptr += chip->bank_size_playback;
			ptr_addr += chip->bank_size_playback;
		}
	}
	ptr += (chip->bank_size_playback + 0x00ff) & ~0x00ff;
	ptr_addr += (chip->bank_size_playback + 0x00ff) & ~0x00ff;
	chip->bank_base_capture = ptr;
	chip->bank_base_capture_addr = ptr_addr;
	for (voice = 0; voice < YDSXG_CAPTURE_VOICES; voice++)
		for (bank = 0; bank < 2; bank++) {
			chip->bank_capture[voice][bank] = (snd_ymfpci_capture_bank_t *)ptr;
			ptr += chip->bank_size_capture;
			ptr_addr += chip->bank_size_capture;
		}
	ptr += (chip->bank_size_capture + 0x00ff) & ~0x00ff;
	ptr_addr += (chip->bank_size_capture + 0x00ff) & ~0x00ff;
	chip->bank_base_effect = ptr;
	chip->bank_base_effect_addr = ptr_addr;
	for (voice = 0; voice < YDSXG_EFFECT_VOICES; voice++)
		for (bank = 0; bank < 2; bank++) {
			chip->bank_effect[voice][bank] = (snd_ymfpci_effect_bank_t *)ptr;
			ptr += chip->bank_size_effect;
			ptr_addr += chip->bank_size_effect;
		}
	ptr += (chip->bank_size_effect + 0x00ff) & ~0x00ff;
	ptr_addr += (chip->bank_size_effect + 0x00ff) & ~0x00ff;
	chip->work_base = ptr;
	chip->work_base_addr = ptr_addr;

	snd_ymfpci_writel(chip, YDSXGR_PLAYCTRLBASE, chip->bank_base_playback_addr);
	snd_ymfpci_writel(chip, YDSXGR_RECCTRLBASE, chip->bank_base_capture_addr);
	snd_ymfpci_writel(chip, YDSXGR_EFFCTRLBASE, chip->bank_base_effect_addr);
	snd_ymfpci_writel(chip, YDSXGR_WORKBASE, chip->work_base_addr);
	snd_ymfpci_writel(chip, YDSXGR_WORKSIZE, chip->work_size >> 2);

	/* S/PDIF output initialization */
	chip->spdif_bits = chip->spdif_pcm_bits = SNDRV_PCM_DEFAULT_CON_SPDIF & 0xffff;
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTCTRL, 0);
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_bits);

	/* S/PDIF input initialization */
	snd_ymfpci_writew(chip, YDSXGR_SPDIFINCTRL, 0);

	/* digital mixer setup */
	for (reg = 0x80; reg < 0xc0; reg += 4)
		snd_ymfpci_writel(chip, reg, 0);
	snd_ymfpci_writel(chip, YDSXGR_NATIVEDACOUTVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_ZVOUTVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_SPDIFOUTVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_NATIVEADCINVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_NATIVEDACINVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_PRIADCLOOPVOL, 0x3fff3fff);
	
	return 0;
}

static int snd_ymfpci_free(ymfpci_t *chip)
{
	u16 ctrl;

	snd_assert(chip != NULL, return -EINVAL);
	snd_ymfpci_proc_done(chip);

	if (chip->res_reg_area) {	/* don't touch busy hardware */
		snd_ymfpci_writel(chip, YDSXGR_NATIVEDACOUTVOL, 0);
		snd_ymfpci_writel(chip, YDSXGR_BUF441OUTVOL, 0);
		snd_ymfpci_writel(chip, YDSXGR_STATUS, ~0);
		snd_ymfpci_disable_dsp(chip);
		snd_ymfpci_writel(chip, YDSXGR_PLAYCTRLBASE, 0);
		snd_ymfpci_writel(chip, YDSXGR_RECCTRLBASE, 0);
		snd_ymfpci_writel(chip, YDSXGR_EFFCTRLBASE, 0);
		snd_ymfpci_writel(chip, YDSXGR_WORKBASE, 0);
		snd_ymfpci_writel(chip, YDSXGR_WORKSIZE, 0);
		ctrl = snd_ymfpci_readw(chip, YDSXGR_GLOBALCTRL);
		snd_ymfpci_writew(chip, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);
	}

	/* Set PCI device to D3 state */
	pci_set_power_state(chip->pci, 3);

#ifdef CONFIG_PM
	if (chip->saved_regs)
		kfree(chip->saved_regs);
#endif
	if (chip->reg_area_virt)
		iounmap((void *)chip->reg_area_virt);
	if (chip->work_ptr)
		snd_free_pci_pages(chip->pci, chip->work_ptr_size, chip->work_ptr, chip->work_ptr_addr);
	
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	if (chip->res_reg_area)
		release_resource(chip->res_reg_area);

	pci_write_config_word(chip->pci, 0x40, chip->old_legacy_ctrl);
	
	snd_magic_kfree(chip);
	return 0;
}

static int snd_ymfpci_dev_free(snd_device_t *device)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, device->device_data, return -ENXIO);
	return snd_ymfpci_free(chip);
}

#ifdef CONFIG_PM
static int saved_regs_index[] = {
	/* spdif */
	YDSXGR_SPDIFOUTCTRL,
	YDSXGR_SPDIFOUTSTATUS,
	YDSXGR_SPDIFINCTRL,
	/* volumes */
	YDSXGR_PRIADCLOOPVOL,
	YDSXGR_NATIVEDACINVOL,
	YDSXGR_NATIVEDACOUTVOL,
	// YDSXGR_BUF441OUTVOL,
	YDSXGR_NATIVEADCINVOL,
	YDSXGR_SPDIFLOOPVOL,
	YDSXGR_SPDIFOUTVOL,
	YDSXGR_ZVOUTVOL,
	/* address bases */
	YDSXGR_PLAYCTRLBASE,
	YDSXGR_RECCTRLBASE,
	YDSXGR_EFFCTRLBASE,
	YDSXGR_WORKBASE,
	/* capture set up */
	YDSXGR_MAPOFREC,
	YDSXGR_RECFORMAT,
	YDSXGR_RECSLOTSR,
	YDSXGR_ADCFORMAT,
	YDSXGR_ADCSLOTSR,
};
#define YDSXGR_NUM_SAVED_REGS	(sizeof(saved_regs_index)/sizeof(saved_regs_index[0]))

void snd_ymfpci_suspend(ymfpci_t *chip)
{
	int i;
	for (i = 0; i < YDSXGR_NUM_SAVED_REGS; i++)
		chip->saved_regs[i] = snd_ymfpci_readl(chip, saved_regs_index[i]);
	snd_ymfpci_writel(chip, YDSXGR_NATIVEDACOUTVOL, 0);
	snd_ymfpci_disable_dsp(chip);
}

void snd_ymfpci_resume(ymfpci_t *chip)
{
	int i;

	pci_set_master(chip->pci);
	snd_ymfpci_aclink_reset(chip->pci);
	snd_ymfpci_codec_ready(chip, 0, 0);
	snd_ymfpci_download_image(chip);
	udelay(100);

	for (i = 0; i < YDSXGR_NUM_SAVED_REGS; i++)
		snd_ymfpci_writel(chip, saved_regs_index[i], chip->saved_regs[i]);

	snd_ac97_resume(chip->ac97);

	/* start hw again */
	if (chip->start_count > 0) {
		snd_ymfpci_writel(chip, YDSXGR_MODE, 3);
		chip->active_bank = snd_ymfpci_readl(chip, YDSXGR_CTRLSELECT);
	}
}
#endif

int snd_ymfpci_create(snd_card_t * card,
		      struct pci_dev * pci,
		      unsigned short old_legacy_ctrl,
		      ymfpci_t ** rchip)
{
	ymfpci_t *chip;
	int err;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_ymfpci_dev_free,0,0
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_ymfpci_dev_free,
	};
#endif	
	*rchip = NULL;

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = snd_magic_kcalloc(ymfpci_t, 0, GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	chip->old_legacy_ctrl = old_legacy_ctrl;
	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->voice_lock);
	init_waitqueue_head(&chip->interrupt_sleep);
	atomic_set(&chip->interrupt_sleep_count, 0);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->device_id = pci->device;
	pci_read_config_byte(pci, PCI_REVISION_ID, (u8 *)&chip->rev);
	chip->reg_area_phys = pci_resource_start(pci, 0);
	chip->reg_area_virt = (unsigned long)ioremap(chip->reg_area_phys, 0x8000);
	pci_set_master(pci);

	if ((chip->res_reg_area = request_mem_region(chip->reg_area_phys, 0x8000, "YMFPCI")) == NULL) {
		snd_ymfpci_free(chip);
		snd_printk("unable to grab memory region 0x%lx-0x%lx\n", chip->reg_area_phys, chip->reg_area_phys + 0x8000 - 1);
		return -EBUSY;
	}
	if (request_irq(pci->irq, snd_ymfpci_interrupt, SA_INTERRUPT|SA_SHIRQ, "YMFPCI", (void *) chip)) {
		snd_ymfpci_free(chip);
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	chip->irq = pci->irq;

	snd_ymfpci_aclink_reset(pci);
	if (snd_ymfpci_codec_ready(chip, 0, 1) < 0) {
		snd_ymfpci_free(chip);
		return -EIO;
	}

	snd_ymfpci_download_image(chip);

	udelay(100); /* seems we need a delay after downloading image.. */

	if (snd_ymfpci_memalloc(chip) < 0) {
		snd_ymfpci_free(chip);
		return -EIO;
	}

#ifdef CONFIG_PM
	chip->saved_regs = kmalloc(YDSXGR_NUM_SAVED_REGS * sizeof(u32), GFP_KERNEL);
	if (chip->saved_regs == NULL) {
		snd_ymfpci_free(chip);
		return -ENOMEM;
	}
#endif

	snd_ymfpci_proc_init(card, chip);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_ymfpci_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;
}

EXPORT_SYMBOL(snd_ymfpci_create);
EXPORT_SYMBOL(snd_ymfpci_interrupt);
EXPORT_SYMBOL(snd_ymfpci_pcm);
EXPORT_SYMBOL(snd_ymfpci_pcm2);
EXPORT_SYMBOL(snd_ymfpci_pcm_spdif);
EXPORT_SYMBOL(snd_ymfpci_pcm_4ch);
EXPORT_SYMBOL(snd_ymfpci_mixer);
#ifdef CONFIG_PM
EXPORT_SYMBOL(snd_ymfpci_suspend);
EXPORT_SYMBOL(snd_ymfpci_resume);
#endif

/*
 *  INIT part
 */

static int __init alsa_ymfpci_init(void)
{
	return 0;
}

static void __exit alsa_ymfpci_exit(void)
{
}

module_init(alsa_ymfpci_init)
module_exit(alsa_ymfpci_exit)
