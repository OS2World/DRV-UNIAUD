/*
 *  Dummy soundcard
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("Dummy soundcard (/dev/null)");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{ALSA,Dummy soundcard}}");

#define MAX_PCM_DEVICES		4
#define MAX_PCM_SUBSTREAMS	16
#define MAX_MIDI_DEVICES	2

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
#ifdef TARGET_OS2
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;
static int snd_pcm_devs[SNDRV_CARDS] = SNDDRV_DEFAULT_PCM_DEVS;
static int snd_pcm_substreams[SNDRV_CARDS] = SNDDRV_DEFAULT_PCM_SUBSTREAMS;
#else
static int snd_enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};
static int snd_pcm_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
static int snd_pcm_substreams[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 8};
#endif
//static int snd_midi_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for dummy soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for dummy soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable this dummy soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_pcm_devs, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_pcm_devs, "PCM devices # (0-4) for dummy driver.");
MODULE_PARM_SYNTAX(snd_pcm_devs, SNDRV_ENABLED ",allows:{{0,4}},default:1,dialog:list");
MODULE_PARM(snd_pcm_substreams, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_pcm_substreams, "PCM substreams # (1-16) for dummy driver.");
MODULE_PARM_SYNTAX(snd_pcm_substreams, SNDRV_ENABLED ",allows:{{1,16}},default:8,dialog:list");
//MODULE_PARM(snd_midi_devs, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
//MODULE_PARM_DESC(snd_midi_devs, "MIDI devices # (0-2) for dummy driver.");
//MODULE_PARM_SYNTAX(snd_midi_devs, SNDRV_ENABLED ",allows:{{0,2}},default:8,dialog:list");

#define MIXER_ADDR_MASTER	0
#define MIXER_ADDR_LINE		1
#define MIXER_ADDR_MIC		2
#define MIXER_ADDR_SYNTH	3
#define MIXER_ADDR_CD		4
#define MIXER_ADDR_LAST		4

typedef struct snd_card_dummy {
	snd_card_t *card;
	spinlock_t mixer_lock;
	int mixer_volume[MIXER_ADDR_LAST+1][2];
	int capture_source[MIXER_ADDR_LAST+1][2];
} snd_card_dummy_t;

typedef struct snd_card_dummy_pcm {
	snd_card_dummy_t *dummy;
	spinlock_t lock;
	struct timer_list timer;
	unsigned int pcm_size;
	unsigned int pcm_count;
	unsigned int pcm_bps;		/* bytes per second */
	unsigned int pcm_jiffie;	/* bytes per one jiffie */
	unsigned int pcm_irq_pos;	/* IRQ position */
	unsigned int pcm_buf_pos;	/* position in buffer */
	snd_pcm_substream_t *substream;
} snd_card_dummy_pcm_t;

static snd_card_t *snd_dummy_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int snd_card_dummy_playback_ioctl(snd_pcm_substream_t * substream,
				         unsigned int cmd,
				         void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_card_dummy_capture_ioctl(snd_pcm_substream_t * substream,
					unsigned int cmd,
					void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static void snd_card_dummy_pcm_timer_start(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_dummy_pcm_t *dpcm = snd_magic_cast(snd_card_dummy_pcm_t, runtime->private_data, return);

	dpcm->timer.expires = 1 + jiffies;
	add_timer(&dpcm->timer);
}

static void snd_card_dummy_pcm_timer_stop(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_dummy_pcm_t *dpcm = snd_magic_cast(snd_card_dummy_pcm_t, runtime->private_data, return);

	del_timer(&dpcm->timer);
}

static int snd_card_dummy_playback_trigger(snd_pcm_substream_t * substream,
					   int cmd)
{
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		snd_card_dummy_pcm_timer_start(substream);
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		snd_card_dummy_pcm_timer_stop(substream);
	} else {
		return -EINVAL;
	}
	return 0;
}

static int snd_card_dummy_capture_trigger(snd_pcm_substream_t * substream,
					  int cmd)
{
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		snd_card_dummy_pcm_timer_start(substream);
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		snd_card_dummy_pcm_timer_stop(substream);
	} else {
		return -EINVAL;
	}
	return 0;
}

static int snd_card_dummy_pcm_prepare(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_dummy_pcm_t *dpcm = snd_magic_cast(snd_card_dummy_pcm_t, runtime->private_data, return -ENXIO);
	unsigned int bps;

	bps = runtime->rate * runtime->channels;
	bps *= snd_pcm_format_width(runtime->format);
	bps /= 8;
	if (bps <= 0)
		return -EINVAL;
	dpcm->pcm_bps = bps;
	dpcm->pcm_jiffie = bps / HZ;
	dpcm->pcm_size = snd_pcm_lib_buffer_bytes(substream);
	dpcm->pcm_count = snd_pcm_lib_period_bytes(substream);
	dpcm->pcm_irq_pos = 0;
	dpcm->pcm_buf_pos = 0;
	return 0;
}

static int snd_card_dummy_playback_prepare(snd_pcm_substream_t * substream)
{
	return snd_card_dummy_pcm_prepare(substream);
}

static int snd_card_dummy_capture_prepare(snd_pcm_substream_t * substream)
{
	return snd_card_dummy_pcm_prepare(substream);
}

static void snd_card_dummy_pcm_timer_function(unsigned long data)
{
	snd_card_dummy_pcm_t *dpcm = snd_magic_cast(snd_card_dummy_pcm_t, (void *)data, return);
	
	dpcm->timer.expires = 1 + jiffies;
	add_timer(&dpcm->timer);
	spin_lock_irq(&dpcm->lock);
	dpcm->pcm_irq_pos += dpcm->pcm_jiffie;
	dpcm->pcm_buf_pos += dpcm->pcm_jiffie;
	dpcm->pcm_buf_pos %= dpcm->pcm_size;
	while (dpcm->pcm_irq_pos >= dpcm->pcm_count) {
		dpcm->pcm_irq_pos -= dpcm->pcm_count;
		snd_pcm_period_elapsed(dpcm->substream);
	}
	spin_unlock_irq(&dpcm->lock);	
}

static snd_pcm_uframes_t snd_card_dummy_playback_pointer(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_dummy_pcm_t *dpcm = snd_magic_cast(snd_card_dummy_pcm_t, runtime->private_data, return -ENXIO);

	return bytes_to_frames(runtime, dpcm->pcm_buf_pos);
}

static snd_pcm_uframes_t snd_card_dummy_capture_pointer(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_dummy_pcm_t *dpcm = snd_magic_cast(snd_card_dummy_pcm_t, runtime->private_data, return -ENXIO);

	return bytes_to_frames(runtime, dpcm->pcm_buf_pos);
}

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_card_dummy_playback =
{
/*	info:		  */   (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
/*	formats:	  */    SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	5500,
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

static snd_pcm_hardware_t snd_card_dummy_capture =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	5500,
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
static snd_pcm_hardware_t snd_card_dummy_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		5500,
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

static snd_pcm_hardware_t snd_card_dummy_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		5500,
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

static void snd_card_dummy_runtime_free(snd_pcm_runtime_t *runtime)
{
	snd_card_dummy_pcm_t *dpcm = snd_magic_cast(snd_card_dummy_pcm_t, runtime->private_data, return);
	snd_magic_kfree(dpcm);
}

static int snd_card_dummy_playback_open(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_dummy_pcm_t *dpcm;

	dpcm = snd_magic_kcalloc(snd_card_dummy_pcm_t, 0, GFP_KERNEL);
	if (dpcm == NULL)
		return -ENOMEM;
	if ((runtime->dma_area = snd_malloc_pages_fallback(64*1024, GFP_KERNEL, &runtime->dma_bytes)) == NULL) {
		snd_magic_kfree(dpcm);
		return -ENOMEM;
	}
	dpcm->timer.data = (unsigned long) dpcm;
	dpcm->timer.function = snd_card_dummy_pcm_timer_function;
	spin_lock_init(&dpcm->lock);
	dpcm->substream = substream;
	runtime->private_data = dpcm;
	runtime->private_free = snd_card_dummy_runtime_free;
	runtime->hw = snd_card_dummy_playback;
	if (substream->pcm->device & 1) {
		runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
		runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
	}
	if (substream->pcm->device & 2)
		runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP|SNDRV_PCM_INFO_MMAP_VALID);
	return 0;
}

static int snd_card_dummy_capture_open(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_dummy_pcm_t *dpcm;

	dpcm = snd_magic_kcalloc(snd_card_dummy_pcm_t, 0, GFP_KERNEL);
	if (dpcm == NULL)
		return -ENOMEM;
	if ((runtime->dma_area = snd_malloc_pages_fallback(64*1024, GFP_KERNEL, &runtime->dma_bytes)) == NULL) {
		snd_magic_kfree(dpcm);
		return -ENOMEM;
	}
	memset(runtime->dma_area, 0, runtime->dma_bytes);
	dpcm->timer.data = (unsigned long) dpcm;
	dpcm->timer.function = snd_card_dummy_pcm_timer_function;
	spin_lock_init(&dpcm->lock);
	dpcm->substream = substream;
	runtime->private_data = dpcm;
	runtime->private_free = snd_card_dummy_runtime_free;
	runtime->hw = snd_card_dummy_capture;
	if (substream->pcm->device == 1) {
		runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
		runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
	}
	if (substream->pcm->device & 2)
		runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP|SNDRV_PCM_INFO_MMAP_VALID);
	return 0;
}

static int snd_card_dummy_playback_close(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_free_pages(runtime->dma_area, runtime->dma_bytes);
	return 0;
}

static int snd_card_dummy_capture_close(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_free_pages(runtime->dma_area, runtime->dma_bytes);
	return 0;
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_card_dummy_playback_ops = {
/*	open:		*/	snd_card_dummy_playback_open,
/*	close:		*/	snd_card_dummy_playback_close,
/*	ioctl:		*/	snd_card_dummy_playback_ioctl,
        NULL, NULL,
/*	prepare:	*/	snd_card_dummy_playback_prepare,
/*	trigger:	*/	snd_card_dummy_playback_trigger,
/*	pointer:	*/	snd_card_dummy_playback_pointer,
        NULL, NULL
};

static snd_pcm_ops_t snd_card_dummy_capture_ops = {
/*	open:		*/	snd_card_dummy_capture_open,
/*	close:		*/	snd_card_dummy_capture_close,
/*	ioctl:		*/	snd_card_dummy_capture_ioctl,
        NULL, NULL,
/*	prepare:	*/	snd_card_dummy_capture_prepare,
/*	trigger:	*/	snd_card_dummy_capture_trigger,
/*	pointer:	*/	snd_card_dummy_capture_pointer,
        NULL, NULL
};
#else
static snd_pcm_ops_t snd_card_dummy_playback_ops = {
	open:			snd_card_dummy_playback_open,
	close:			snd_card_dummy_playback_close,
	ioctl:			snd_card_dummy_playback_ioctl,
	prepare:		snd_card_dummy_playback_prepare,
	trigger:		snd_card_dummy_playback_trigger,
	pointer:		snd_card_dummy_playback_pointer,
};

static snd_pcm_ops_t snd_card_dummy_capture_ops = {
	open:			snd_card_dummy_capture_open,
	close:			snd_card_dummy_capture_close,
	ioctl:			snd_card_dummy_capture_ioctl,
	prepare:		snd_card_dummy_capture_prepare,
	trigger:		snd_card_dummy_capture_trigger,
	pointer:		snd_card_dummy_capture_pointer,
};
#endif

static int __init snd_card_dummy_pcm(snd_card_dummy_t *dummy, int device, int substreams)
{
	snd_pcm_t *pcm;
	int err;

	if ((err = snd_pcm_new(dummy->card, "Dummy PCM", device, substreams, substreams, &pcm)) < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_card_dummy_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_card_dummy_capture_ops);
	pcm->private_data = dummy;
	pcm->info_flags = 0;
	strcpy(pcm->name, "Dummy PCM");
	return 0;
}
#ifdef TARGET_OS2
#define DUMMY_VOLUME(xname, xindex, addr) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, xindex, \
  0, snd_dummy_volume_info, \
  snd_dummy_volume_get, snd_dummy_volume_put, \
  addr }
#else
#define DUMMY_VOLUME(xname, xindex, addr) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_dummy_volume_info, \
  get: snd_dummy_volume_get, put: snd_dummy_volume_put, \
  private_value: addr }
#endif

static int snd_dummy_volume_info(snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	return 0;
}
 
static int snd_dummy_volume_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_card_dummy_t *dummy = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr = kcontrol->private_value;

	spin_lock_irqsave(&dummy->mixer_lock, flags);
	ucontrol->value.integer.value[0] = dummy->mixer_volume[addr][0];
	ucontrol->value.integer.value[1] = dummy->mixer_volume[addr][1];
	spin_unlock_irqrestore(&dummy->mixer_lock, flags);
	return 0;
}                                                                                                                                                                                                                                                                                                            

static int snd_dummy_volume_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_card_dummy_t *dummy = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change, addr = kcontrol->private_value;
	int left, right;

	left = ucontrol->value.integer.value[0] % 101;
	right = ucontrol->value.integer.value[1] % 101;
	spin_lock_irqsave(&dummy->mixer_lock, flags);
	change = dummy->mixer_volume[addr][0] != left &&
	         dummy->mixer_volume[addr][1] != right;
	dummy->mixer_volume[addr][0] = left;
	dummy->mixer_volume[addr][1] = right;
	spin_unlock_irqrestore(&dummy->mixer_lock, flags);
	return change;
}                                                                                                                                                                                                                                                                                                            

#ifdef TARGET_OS2
#define DUMMY_CAPSRC(xname, xindex, addr) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, xindex, \
  0, snd_dummy_capsrc_info, \
  snd_dummy_capsrc_get, snd_dummy_capsrc_put, \
  addr }
#else
#define DUMMY_CAPSRC(xname, xindex, addr) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_dummy_capsrc_info, \
  get: snd_dummy_capsrc_get, put: snd_dummy_capsrc_put, \
  private_value: addr }
#endif

static int snd_dummy_capsrc_info(snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}
 
static int snd_dummy_capsrc_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_card_dummy_t *dummy = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr = kcontrol->private_value;

	spin_lock_irqsave(&dummy->mixer_lock, flags);
	ucontrol->value.integer.value[0] = dummy->capture_source[addr][0];
	ucontrol->value.integer.value[1] = dummy->capture_source[addr][1];
	spin_unlock_irqrestore(&dummy->mixer_lock, flags);
	return 0;
}

static int snd_dummy_capsrc_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_card_dummy_t *dummy = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change, addr = kcontrol->private_value;
	int left, right;

	left = ucontrol->value.integer.value[0] & 1;
	right = ucontrol->value.integer.value[1] & 1;
	spin_lock_irqsave(&dummy->mixer_lock, flags);
	change = dummy->capture_source[addr][0] != left &&
	         dummy->capture_source[addr][1] != right;
	dummy->capture_source[addr][0] = left;
	dummy->capture_source[addr][1] = right;
	spin_unlock_irqrestore(&dummy->mixer_lock, flags);
	return change;
}                                                                                                                                                                                                                                                                                                            

#define DUMMY_CONTROLS (sizeof(snd_dummy_controls)/sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_dummy_controls[] = {
DUMMY_VOLUME("Master Volume", 0, MIXER_ADDR_MASTER),
DUMMY_CAPSRC("Master Capture Switch", 0, MIXER_ADDR_MASTER),
DUMMY_VOLUME("Synth Volume", 0, MIXER_ADDR_SYNTH),
DUMMY_CAPSRC("Synth Capture Switch", 0, MIXER_ADDR_MASTER),
DUMMY_VOLUME("Line Volume", 0, MIXER_ADDR_LINE),
DUMMY_CAPSRC("Line Capture Switch", 0, MIXER_ADDR_MASTER),
DUMMY_VOLUME("Mic Volume", 0, MIXER_ADDR_MIC),
DUMMY_CAPSRC("Mic Capture Switch", 0, MIXER_ADDR_MASTER),
DUMMY_VOLUME("CD Volume", 0, MIXER_ADDR_CD),
DUMMY_CAPSRC("CD Capture Switch", 0, MIXER_ADDR_MASTER)
};

int __init snd_card_dummy_new_mixer(snd_card_dummy_t * dummy)
{
	snd_card_t *card = dummy->card;
	int idx, err;

	snd_assert(dummy != NULL, return -EINVAL);
	spin_lock_init(&dummy->mixer_lock);
	strcpy(card->mixername, "Dummy Mixer");

	for (idx = 0; idx < DUMMY_CONTROLS; idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_dummy_controls[idx], dummy))) < 0)
			return err;
	}
	return 0;
}

static int __init snd_card_dummy_probe(int dev)
{
	snd_card_t *card;
	struct snd_card_dummy *dummy;
	int idx, err;

	if (!snd_enable[dev])
		return -ENODEV;
	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE,
			    sizeof(struct snd_card_dummy));
	if (card == NULL)
		return -ENOMEM;
	dummy = (struct snd_card_dummy *)card->private_data;
	dummy->card = card;
	for (idx = 0; idx < MAX_PCM_DEVICES && idx < snd_pcm_devs[dev]; idx++) {
		if (snd_pcm_substreams[dev] < 1)
			snd_pcm_substreams[dev] = 1;
		if (snd_pcm_substreams[dev] > MAX_PCM_SUBSTREAMS)
			snd_pcm_substreams[dev] = MAX_PCM_SUBSTREAMS;
		if ((err = snd_card_dummy_pcm(dummy, idx, snd_pcm_substreams[dev])) < 0)
			goto __nodev;
	}
	if ((err = snd_card_dummy_new_mixer(dummy)) < 0)
		goto __nodev;
	strcpy(card->driver, "Dummy");
	strcpy(card->shortname, "Dummy");
	sprintf(card->longname, "Dummy %i", dev + 1);
	if ((err = snd_card_register(card)) == 0) {
		snd_dummy_cards[dev] = card;
		return 0;
	}
      __nodev:
	snd_card_free(card);
	return err;
}

static int __init alsa_card_dummy_init(void)
{
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && snd_enable[dev]; dev++) {
		if (snd_card_dummy_probe(dev) < 0) {
#ifdef MODULE
			snd_printk("Dummy soundcard #%i not found or device busy\n", dev + 1);
#endif
			break;
		}
		cards++;
	}
	if (!cards) {
#ifdef MODULE
		snd_printk("Dummy soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_dummy_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_dummy_cards[idx]);
}

module_init(alsa_card_dummy_init)
module_exit(alsa_card_dummy_exit)

#ifndef MODULE

/* format is: snd-card-dummy=snd_enable,snd_index,snd_id,
			     snd_pcm_devs,snd_pcm_substreams */

static int __init alsa_card_dummy_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,&snd_pcm_devs[nr_dev]) == 2 &&
	       get_option(&str,&snd_pcm_substreams[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-dummy=", alsa_card_dummy_setup);

#endif /* ifndef MODULE */
