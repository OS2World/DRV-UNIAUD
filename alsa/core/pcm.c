/*
 *  Digital Audio (PCM) abstract layer
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

#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#include <sound/minors.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/info.h>

snd_pcm_t *snd_pcm_devices[SNDRV_CARDS * SNDRV_PCM_DEVICES];
static LIST_HEAD(snd_pcm_notify_list);
static DECLARE_MUTEX(register_mutex);

int snd_pcm_free(snd_pcm_t *pcm);
static int snd_pcm_dev_free(snd_device_t *device);
static int snd_pcm_dev_register(snd_device_t *device);
static int snd_pcm_dev_unregister(snd_device_t *device);

void snd_pcm_lock(int xup)
{
	if (!xup) {
		down(&register_mutex);
	} else {
		up(&register_mutex);
	}
}

static int snd_pcm_control_ioctl(snd_card_t * card,
				 snd_ctl_file_t * control,
				 unsigned int cmd, unsigned long arg)
{
	unsigned int tmp;

	tmp = card->number * SNDRV_PCM_DEVICES;
	switch (cmd) {
	case SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE:
		{
			int device;

			if (get_user(device, (int *)arg))
				return -EFAULT;
			device = device < 0 ? 0 : device + 1;
			while (device < SNDRV_PCM_DEVICES) {
				if (snd_pcm_devices[tmp + device])
					break;
				device++;
			}
			if (device == SNDRV_PCM_DEVICES)
				device = -1;
			if (put_user(device, (int *)arg))
				return -EFAULT;
			return 0;
		}
	case SNDRV_CTL_IOCTL_PCM_INFO:
		{
			snd_pcm_info_t *info = (snd_pcm_info_t *)arg;
			unsigned int device, subdevice;
			snd_pcm_stream_t stream;
			snd_pcm_t *pcm;
			snd_pcm_str_t *pstr;
			snd_pcm_substream_t *substream;
			if (get_user(device, &info->device))
				return -EFAULT;
			if (device >= SNDRV_PCM_DEVICES)
				return -ENXIO;
			pcm = snd_pcm_devices[tmp + device];
			if (pcm == NULL)
				return -ENXIO;
			if (get_user(stream, &info->stream))
				return -EFAULT;
			if (stream < 0 || stream > 1)
				return -EINVAL;
			pstr = &pcm->streams[stream];
			if (pstr->substream_count == 0)
				return -ENOENT;
			if (get_user(subdevice, &info->subdevice))
				return -EFAULT;
			if (subdevice >= pstr->substream_count)
				return -ENXIO;
			for (substream = pstr->substream; substream; substream = substream->next)
				if (substream->number == subdevice)
					break;
			if (substream == NULL)
				return -ENXIO;
			return snd_pcm_info_user(substream, info);
		}
	case SNDRV_CTL_IOCTL_PCM_PREFER_SUBDEVICE:
		{
			int val;
			
			if (get_user(val, (int *)arg))
				return -EFAULT;
			control->prefer_pcm_subdevice = val;
			return 0;
		}
	}
	return -ENOIOCTLCMD;
}
#define STATE(v) [SNDRV_PCM_STATE_##v] = #v
#define STREAM(v) [SNDRV_PCM_STREAM_##v] = #v
#define READY(v) [SNDRV_PCM_READY_##v] = #v
#define XRUN(v) [SNDRV_PCM_XRUN_##v] = #v
#define SILENCE(v) [SNDRV_PCM_SILENCE_##v] = #v
#define TSTAMP(v) [SNDRV_PCM_TSTAMP_##v] = #v
#define ACCESS(v) [SNDRV_PCM_ACCESS_##v] = #v
#define START(v) [SNDRV_PCM_START_##v] = #v
#define FORMAT(v) [SNDRV_PCM_FORMAT_##v] = #v
#define SUBFORMAT(v) [SNDRV_PCM_SUBFORMAT_##v] = #v 

#ifdef TARGET_OS2
char *snd_pcm_stream_names[] = {
	"PLAYBACK",
	"CAPTURE",
};

char *snd_pcm_state_names[] = {
	"OPEN",
	"SETUP",
	"PREPARED",
	"RUNNING",
	"XRUN",
        NULL,
	"PAUSED",
};

char *snd_pcm_access_names[] = {
	"MMAP_INTERLEAVED", 
	"MMAP_NONINTERLEAVED",
	"MMAP_COMPLEX",
	"RW_INTERLEAVED",
	"RW_NONINTERLEAVED",
};

char *snd_pcm_format_names[] = {
	"S8",
	"U8",
	"S16_LE",
	"S16_BE",
	"U16_LE",
	"U16_BE",
	"S24_LE",
	"S24_BE",
	"U24_LE",
	"U24_BE",
	"S32_LE",
	"S32_BE",
	"U32_LE",
	"U32_BE",
	"FLOAT_LE",
	"FLOAT_BE",
	"FLOAT64_LE",
	"FLOAT64_BE",
	"IEC958_SUBFRAME_LE",
	"IEC958_SUBFRAME_BE",
	"MU_LAW",
	"A_LAW",
	"IMA_ADPCM",
	"MPEG",
	"GSM",
        NULL,NULL,NULL,NULL,NULL,
	"SPECIAL",
};

char *snd_pcm_subformat_names[] = {
	"STD", 
};

char *snd_pcm_tstamp_mode_names[] = {
	"NONE",
	"MMAP",
};
#else
char *snd_pcm_stream_names[] = {
	STREAM(PLAYBACK),
	STREAM(CAPTURE),
};

char *snd_pcm_state_names[] = {
	STATE(OPEN),
	STATE(SETUP),
	STATE(PREPARED),
	STATE(RUNNING),
	STATE(XRUN),
	STATE(PAUSED),
};

char *snd_pcm_access_names[] = {
	ACCESS(MMAP_INTERLEAVED), 
	ACCESS(MMAP_NONINTERLEAVED),
	ACCESS(MMAP_COMPLEX),
	ACCESS(RW_INTERLEAVED),
	ACCESS(RW_NONINTERLEAVED),
};

char *snd_pcm_format_names[] = {
	FORMAT(S8),
	FORMAT(U8),
	FORMAT(S16_LE),
	FORMAT(S16_BE),
	FORMAT(U16_LE),
	FORMAT(U16_BE),
	FORMAT(S24_LE),
	FORMAT(S24_BE),
	FORMAT(U24_LE),
	FORMAT(U24_BE),
	FORMAT(S32_LE),
	FORMAT(S32_BE),
	FORMAT(U32_LE),
	FORMAT(U32_BE),
	FORMAT(FLOAT_LE),
	FORMAT(FLOAT_BE),
	FORMAT(FLOAT64_LE),
	FORMAT(FLOAT64_BE),
	FORMAT(IEC958_SUBFRAME_LE),
	FORMAT(IEC958_SUBFRAME_BE),
	FORMAT(MU_LAW),
	FORMAT(A_LAW),
	FORMAT(IMA_ADPCM),
	FORMAT(MPEG),
	FORMAT(GSM),
	FORMAT(SPECIAL),
};

char *snd_pcm_subformat_names[] = {
	SUBFORMAT(STD), 
};

char *snd_pcm_tstamp_mode_names[] = {
	TSTAMP(NONE),
	TSTAMP(MMAP),
};
#endif

const char *snd_pcm_stream_name(snd_pcm_stream_t stream)
{
	snd_assert(stream <= SNDRV_PCM_STREAM_LAST, return 0);
	return snd_pcm_stream_names[stream];
}

const char *snd_pcm_access_name(snd_pcm_access_t access)
{
	snd_assert(access <= SNDRV_PCM_ACCESS_LAST, return 0);
	return snd_pcm_access_names[access];
}

const char *snd_pcm_format_name(snd_pcm_format_t format)
{
	snd_assert(format <= SNDRV_PCM_FORMAT_LAST, return 0);
	return snd_pcm_format_names[format];
}

const char *snd_pcm_subformat_name(snd_pcm_subformat_t subformat)
{
	snd_assert(subformat <= SNDRV_PCM_SUBFORMAT_LAST, return 0);
	return snd_pcm_subformat_names[subformat];
}

const char *snd_pcm_tstamp_mode_name(snd_pcm_tstamp_t mode)
{
	snd_assert(mode <= SNDRV_PCM_TSTAMP_LAST, return 0);
	return snd_pcm_tstamp_mode_names[mode];
}

const char *snd_pcm_state_name(snd_pcm_state_t state)
{
	snd_assert(state <= SNDRV_PCM_STATE_LAST, return 0);
	return snd_pcm_state_names[state];
}

#ifdef CONFIG_SND_OSSEMUL
#include <linux/soundcard.h>
const char *snd_pcm_oss_format_name(int format)
{
	switch (format) {
	case AFMT_MU_LAW:
		return "MU_LAW";
	case AFMT_A_LAW:
		return "A_LAW";
	case AFMT_IMA_ADPCM:
		return "IMA_ADPCM";
	case AFMT_U8:
		return "U8";
	case AFMT_S16_LE:
		return "S16_LE";
	case AFMT_S16_BE:
		return "S16_BE";
	case AFMT_S8:
		return "S8";
	case AFMT_U16_LE:
		return "U16_LE";
	case AFMT_U16_BE:
		return "U16_BE";
	case AFMT_MPEG:
		return "MPEG";
	default:
		return "unknown";
	}
}
#endif


static void snd_pcm_proc_info_read(snd_pcm_substream_t *substream, snd_info_buffer_t *buffer)
{
	snd_pcm_info_t info;
	int err;
	snd_runtime_check(substream, return);
	err = snd_pcm_info(substream, &info);
	if (err < 0) {
		snd_iprintf(buffer, "error %d\n", err);
		return;
	}
	snd_iprintf(buffer, "card: %d\n", info.card);
	snd_iprintf(buffer, "device: %d\n", info.device);
	snd_iprintf(buffer, "subdevice: %d\n", info.subdevice);
	snd_iprintf(buffer, "stream: %s\n", snd_pcm_stream_name(info.stream));
	snd_iprintf(buffer, "id: %s\n", info.id);
	snd_iprintf(buffer, "name: %s\n", info.name);
	snd_iprintf(buffer, "subname: %s\n", info.subname);
	snd_iprintf(buffer, "class: %d\n", info.dev_class);
	snd_iprintf(buffer, "subclass: %d\n", info.dev_subclass);
	snd_iprintf(buffer, "subdevices_count: %d\n", info.subdevices_count);
	snd_iprintf(buffer, "subdevices_avail: %d\n", info.subdevices_avail);
}

static void snd_pcm_stream_proc_info_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	snd_pcm_proc_info_read(((snd_pcm_str_t *)entry->private_data)->substream, buffer);
}

static void snd_pcm_substream_proc_info_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	snd_pcm_proc_info_read((snd_pcm_substream_t *)entry->private_data, buffer);
}

static void snd_pcm_substream_proc_hw_params_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)entry->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (!runtime) {
		snd_iprintf(buffer, "closed\n");
		return;
	}
	spin_lock_irq(&runtime->lock);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		snd_iprintf(buffer, "no setup\n");
		spin_unlock_irq(&runtime->lock);
		return;
	}
	snd_iprintf(buffer, "access: %s\n", snd_pcm_access_name(runtime->access));
	snd_iprintf(buffer, "format: %s\n", snd_pcm_format_name(runtime->format));
	snd_iprintf(buffer, "subformat: %s\n", snd_pcm_subformat_name(runtime->subformat));
	snd_iprintf(buffer, "channels: %u\n", runtime->channels);	
	snd_iprintf(buffer, "rate: %u (%u/%u)\n", runtime->rate, runtime->rate_num, runtime->rate_den);	
	snd_iprintf(buffer, "period_size: %lu\n", runtime->period_size);	
	snd_iprintf(buffer, "buffer_size: %lu\n", runtime->buffer_size);	
	snd_iprintf(buffer, "tick_time: %u\n", runtime->tick_time);
#ifdef CONFIG_SND_OSSEMUL
	if (substream->oss.oss) {
		snd_iprintf(buffer, "OSS format: %s\n", snd_pcm_oss_format_name(runtime->oss.format));
		snd_iprintf(buffer, "OSS channels: %u\n", runtime->oss.channels);	
		snd_iprintf(buffer, "OSS rate: %u\n", runtime->oss.rate);
		snd_iprintf(buffer, "OSS period bytes: %lu\n", (unsigned long)runtime->oss.period_bytes);	
		snd_iprintf(buffer, "OSS periods: %u\n", runtime->oss.periods);
	}
#endif
	spin_unlock_irq(&runtime->lock);
}

static void snd_pcm_substream_proc_sw_params_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)entry->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (!runtime) {
		snd_iprintf(buffer, "closed\n");
		return;
	}
	spin_lock_irq(&runtime->lock);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		snd_iprintf(buffer, "no setup\n");
		spin_unlock_irq(&runtime->lock);
		return;
	}
	snd_iprintf(buffer, "tstamp_mode: %s\n", snd_pcm_tstamp_mode_name(runtime->tstamp_mode));
	snd_iprintf(buffer, "period_step: %u\n", runtime->period_step);
	snd_iprintf(buffer, "sleep_min: %u\n", runtime->sleep_min);
	snd_iprintf(buffer, "avail_min: %lu\n", runtime->control->avail_min);
	snd_iprintf(buffer, "xfer_align: %lu\n", runtime->xfer_align);
	snd_iprintf(buffer, "start_threshold: %lu\n", runtime->start_threshold);
	snd_iprintf(buffer, "stop_threshold: %lu\n", runtime->stop_threshold);
	snd_iprintf(buffer, "silence_threshold: %lu\n", runtime->silence_threshold);
	snd_iprintf(buffer, "silence_size: %lu\n", runtime->silence_size);
	snd_iprintf(buffer, "boundary: %lu\n", runtime->boundary);
	spin_unlock_irq(&runtime->lock);
}

static void snd_pcm_substream_proc_status_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)entry->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_status_t status;
	int err;
	if (!runtime) {
		snd_iprintf(buffer, "closed\n");
		return;
	}
	err = snd_pcm_status(substream, &status);
	if (err < 0) {
		snd_iprintf(buffer, "error %d\n", err);
		return;
	}
	snd_iprintf(buffer, "state: %s\n", snd_pcm_state_name(status.state));
	snd_iprintf(buffer, "trigger_time: %ld.%06ld\n",
		status.trigger_tstamp.tv_sec, status.trigger_tstamp.tv_usec);
	snd_iprintf(buffer, "tstamp      : %ld.%06ld\n",
		status.tstamp.tv_sec, status.tstamp.tv_usec);
	snd_iprintf(buffer, "delay       : %ld\n", status.delay);
	snd_iprintf(buffer, "avail       : %ld\n", status.avail);
	snd_iprintf(buffer, "avail_max   : %ld\n", status.avail_max);
}

static int snd_pcm_stream_proc_init(snd_pcm_str_t *pstr)
{
	snd_pcm_t *pcm = pstr->pcm;
	snd_info_entry_t *entry;
	char name[16];

	sprintf(name, "pcm%i%c", pcm->device, 
		pstr->stream == SNDRV_PCM_STREAM_PLAYBACK ? 'p' : 'c');
	if ((entry = snd_info_create_card_entry(pcm->card, name, pcm->card->proc_root)) == NULL)
		return -ENOMEM;
	entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	pstr->proc_root = entry;

	if ((entry = snd_info_create_card_entry(pcm->card, "info", pstr->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->mode = S_IFREG | S_IRUGO;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_pcm_stream_proc_info_read;
		entry->private_data = pstr;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	pstr->proc_info_entry = entry;

	return 0;
}

static int snd_pcm_stream_proc_done(snd_pcm_str_t *pstr)
{
	if (pstr->proc_info_entry) {
		snd_info_unregister(pstr->proc_info_entry);
		pstr->proc_info_entry = NULL;
	}
	if (pstr->proc_root) {
		snd_info_unregister(pstr->proc_root);
		pstr->proc_root = NULL;
	}
	return 0;
}

static int snd_pcm_substream_proc_init(snd_pcm_substream_t *substream)
{
	snd_info_entry_t *entry;
	snd_card_t *card;
	char name[16];

	card = substream->pcm->card;

	sprintf(name, "sub%i", substream->number);
	if ((entry = snd_info_create_card_entry(card, name, substream->pstr->proc_root)) == NULL)
		return -ENOMEM;
	entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	substream->proc_root = entry;

	if ((entry = snd_info_create_card_entry(card, "info", substream->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->mode = S_IFREG | S_IRUGO;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_pcm_substream_proc_info_read;
		entry->private_data = substream;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	substream->proc_info_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "hw_params", substream->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->mode = S_IFREG | S_IRUGO;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_pcm_substream_proc_hw_params_read;
		entry->private_data = substream;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	substream->proc_hw_params_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "sw_params", substream->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->mode = S_IFREG | S_IRUGO;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_pcm_substream_proc_sw_params_read;
		entry->private_data = substream;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	substream->proc_sw_params_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "status", substream->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->mode = S_IFREG | S_IRUGO;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_pcm_substream_proc_status_read;
		entry->private_data = substream;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	substream->proc_status_entry = entry;

	return 0;
}
		
static int snd_pcm_substream_proc_done(snd_pcm_substream_t *substream)
{
	if (substream->proc_info_entry) {
		snd_info_unregister(substream->proc_info_entry);
		substream->proc_info_entry = 0;
	}
	if (substream->proc_hw_params_entry) {
		snd_info_unregister(substream->proc_hw_params_entry);
		substream->proc_hw_params_entry = 0;
	}
	if (substream->proc_sw_params_entry) {
		snd_info_unregister(substream->proc_sw_params_entry);
		substream->proc_sw_params_entry = 0;
	}
	if (substream->proc_status_entry) {
		snd_info_unregister(substream->proc_status_entry);
		substream->proc_status_entry = 0;
	}
	if (substream->proc_root) {
		snd_info_unregister(substream->proc_root);
		substream->proc_root = 0;
	}
	return 0;
}

static int snd_pcm_new_stream(snd_pcm_t *pcm,
			      snd_pcm_str_t *pstr,
			      int substream_count,
			      int stream)
{
	int idx, err;
	snd_pcm_substream_t *substream, *prev;

#ifdef CONFIG_SND_OSSEMUL
	init_MUTEX(&pstr->oss.setup_mutex);
#endif
	pstr->stream = stream;
	pstr->pcm = pcm;
	pstr->substream_count = substream_count;
	pstr->reg = &snd_pcm_reg[stream];
	if (substream_count > 0) {
		err = snd_pcm_stream_proc_init(pstr);
		if (err < 0)
			return err;
	}
	prev = NULL;
	for (idx = 0, prev = NULL; idx < substream_count; idx++) {
		substream = snd_magic_kcalloc(snd_pcm_substream_t, 0, GFP_KERNEL);
		if (substream == NULL)
			return -ENOMEM;
		substream->pcm = pcm;
		substream->pstr = pstr;
		substream->number = idx;
		substream->stream = stream;
		sprintf(substream->name, "subdevice #%i", idx);
		substream->buffer_bytes_max = UINT_MAX;
		if (prev == NULL)
			pstr->substream = substream;
		else
			prev->next = substream;
		substream->link_next = substream;
		substream->link_prev = substream;
		err = snd_pcm_substream_proc_init(substream);
		if (err < 0) {
			snd_magic_kfree(substream);
			return err;
		}
		substream->dma_type = SNDRV_PCM_DMA_TYPE_CONTINUOUS;
		substream->dma_private = (void *)(unsigned long)GFP_KERNEL;
		prev = substream;
	}
	return 0;
}				

int snd_pcm_new(snd_card_t * card, char *id, int device,
	        int playback_count, int capture_count,
	        snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_pcm_dev_free,
		snd_pcm_dev_register,
		snd_pcm_dev_unregister
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_pcm_dev_free,
		dev_register:	snd_pcm_dev_register,
		dev_unregister:	snd_pcm_dev_unregister
	};
#endif

	snd_assert(rpcm != NULL, return -EINVAL);
	*rpcm = NULL;
	snd_assert(card != NULL, return -ENXIO);
	pcm = snd_magic_kcalloc(snd_pcm_t, 0, GFP_KERNEL);
	if (pcm == NULL)
		return -ENOMEM;
	pcm->card = card;
	pcm->device = device;
	if (id) {
		strncpy(pcm->id, id, sizeof(pcm->id) - 1);
	}
	if ((err = snd_pcm_new_stream(pcm, &pcm->streams[SNDRV_PCM_STREAM_PLAYBACK], playback_count, SNDRV_PCM_STREAM_PLAYBACK)) < 0) {
		snd_pcm_free(pcm);
		return err;
	}
	if ((err = snd_pcm_new_stream(pcm, &pcm->streams[SNDRV_PCM_STREAM_CAPTURE], capture_count, SNDRV_PCM_STREAM_CAPTURE)) < 0) {
		snd_pcm_free(pcm);
		return err;
	}
	init_MUTEX(&pcm->open_mutex);
	init_waitqueue_head(&pcm->open_wait);
	if ((err = snd_device_new(card, SNDRV_DEV_PCM, pcm, &ops)) < 0) {
		snd_pcm_free(pcm);
		return err;
	}
	*rpcm = pcm;
	return 0;
}

static void snd_pcm_free_stream(snd_pcm_str_t * pstr)
{
	snd_pcm_substream_t *substream, *substream_next;
#ifdef CONFIG_SND_OSSEMUL
	snd_pcm_oss_setup_t *setup, *setupn;
#endif
	snd_pcm_stream_proc_done(pstr);
	substream = pstr->substream;
	while (substream) {
		substream_next = substream->next;
		snd_pcm_substream_proc_done(substream);
		snd_magic_kfree(substream);
		substream = substream_next;
	}
#ifdef CONFIG_SND_OSSEMUL
	for (setup = pstr->oss.setup_list; setup; setup = setupn) {
		setupn = setup->next;
		kfree(setup->task_name);
		kfree(setup);
	}
#endif
}

int snd_pcm_free(snd_pcm_t *pcm)
{
	snd_assert(pcm != NULL, return -ENXIO);
	if (pcm->private_free)
		pcm->private_free(pcm);
	snd_pcm_free_stream(&pcm->streams[SNDRV_PCM_STREAM_PLAYBACK]);
	snd_pcm_free_stream(&pcm->streams[SNDRV_PCM_STREAM_CAPTURE]);
	snd_magic_kfree(pcm);
	return 0;
}

int snd_pcm_dev_free(snd_device_t *device)
{
	snd_pcm_t *pcm = snd_magic_cast(snd_pcm_t, device->device_data, return -ENXIO);
	return snd_pcm_free(pcm);
}

static void snd_pcm_tick_timer_func(unsigned long data)
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t*) data;
	snd_pcm_tick_elapsed(substream);
}

int snd_pcm_open_substream(snd_pcm_t *pcm, int stream,
			   snd_pcm_substream_t **rsubstream)
{
	snd_pcm_str_t * pstr;
	snd_pcm_substream_t * substream;
	snd_pcm_runtime_t * runtime;
	snd_ctl_file_t *kctl;
	snd_card_t *card;
	struct list_head *list;
	int prefer_subdevice = -1;
	size_t size;

	snd_assert(rsubstream != NULL, return -EINVAL);
	*rsubstream = NULL;
	snd_assert(pcm != NULL, return -ENXIO);
	pstr = &pcm->streams[stream];
	if (pstr->substream == NULL)
		return -ENODEV;

	card = pcm->card;
	read_lock(&card->control_rwlock);
	list_for_each(list, &card->ctl_files) {
		kctl = snd_ctl_file(list);
		if (kctl->pid == current->pid) {
			prefer_subdevice = kctl->prefer_pcm_subdevice;
			break;
		}
	}
	read_unlock(&card->control_rwlock);

	if (pstr->substream_count == 0)
		return -ENODEV;
	switch (stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		if (pcm->info_flags & SNDRV_PCM_INFO_HALF_DUPLEX) {
			for (substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; substream; substream = substream->next) {
				if (SUBSTREAM_BUSY(substream))
					return -EAGAIN;
			}
		}
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		if (pcm->info_flags & SNDRV_PCM_INFO_HALF_DUPLEX) {
			for (substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; substream; substream = substream->next) {
				if (SUBSTREAM_BUSY(substream))
					return -EAGAIN;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	if (prefer_subdevice >= 0) {
		for (substream = pstr->substream; substream; substream = substream->next)
			if (!SUBSTREAM_BUSY(substream) && substream->number == prefer_subdevice)
				goto __ok;
	}
	for (substream = pstr->substream; substream; substream = substream->next)
		if (!SUBSTREAM_BUSY(substream))
			break;
      __ok:
	if (substream == NULL)
		return -EAGAIN;

	runtime = snd_kcalloc(sizeof(snd_pcm_runtime_t), GFP_KERNEL);
	if (runtime == NULL)
		return -ENOMEM;

	size = PAGE_ALIGN(sizeof(snd_pcm_mmap_status_t));
	runtime->status = snd_malloc_pages(size, GFP_KERNEL);
	if (runtime->status == NULL) {
		kfree(runtime);
		return -ENOMEM;
	}

	size = PAGE_ALIGN(sizeof(snd_pcm_mmap_control_t));
	runtime->control = snd_malloc_pages(size, GFP_KERNEL);
	if (runtime->control == NULL) {
		kfree((void *)runtime->status);
		kfree(runtime);
		return -ENOMEM;
	}

	memset((void*)runtime->status, 0, size);
	memset((void*)runtime->control, 0, size);

	init_waitqueue_head(&runtime->sleep);
	spin_lock_init(&runtime->lock);
	spin_lock_init(&runtime->timer_lock);
	atomic_set(&runtime->mmap_count, 0);
	init_timer(&runtime->tick_timer);
	runtime->tick_timer.function = snd_pcm_tick_timer_func;
	runtime->tick_timer.data = (unsigned long) substream;

	runtime->status->state = SNDRV_PCM_STATE_OPEN;

	substream->runtime = runtime;
	pstr->substream_opened++;
	*rsubstream = substream;
	return 0;
}

void snd_pcm_release_substream(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t * runtime;
	substream->file = NULL;
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return);
	if (runtime->private_free != NULL)
		runtime->private_free(runtime);
	snd_free_pages((void*)runtime->status, PAGE_ALIGN(sizeof(snd_pcm_mmap_status_t)));
	snd_free_pages((void*)runtime->control, PAGE_ALIGN(sizeof(snd_pcm_mmap_control_t)));
	kfree(runtime->hw_constraints.rules);
	kfree(runtime);
	substream->runtime = NULL;
	substream->pstr->substream_opened--;
}

int snd_pcm_dev_register(snd_device_t *device)
{
	int idx, cidx, err;
	unsigned short minor;
	snd_pcm_substream_t *substream;
	struct list_head *list;
	char str[16];
	snd_pcm_t *pcm = snd_magic_cast(snd_pcm_t, device->device_data, return -ENXIO);

	snd_assert(pcm != NULL && device != NULL, return -ENXIO);
	snd_pcm_lock(0);
	idx = (pcm->card->number * SNDRV_PCM_DEVICES) + pcm->device;
	if (snd_pcm_devices[idx]) {
		snd_pcm_lock(1);
		return -EBUSY;
	}
	snd_pcm_devices[idx] = pcm;
	for (cidx = 0; cidx < 2; cidx++) {
		int devtype = -1;
		if (pcm->streams[cidx].substream == NULL)
			continue;
		switch (cidx) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			sprintf(str, "pcmC%iD%ip", pcm->card->number, pcm->device);
			minor = SNDRV_MINOR_PCM_PLAYBACK + idx;
			devtype = SNDRV_DEVICE_TYPE_PCM_PLAYBACK;
			break;
		case SNDRV_PCM_STREAM_CAPTURE:
			sprintf(str, "pcmC%iD%ic", pcm->card->number, pcm->device);
			minor = SNDRV_MINOR_PCM_CAPTURE + idx;
			devtype = SNDRV_DEVICE_TYPE_PCM_CAPTURE;
			break;
		}
		if ((err = snd_register_device(devtype, pcm->card, pcm->device, pcm->streams[cidx].reg, str)) < 0) {
			snd_pcm_devices[idx] = NULL;
			snd_pcm_lock(1);
			return err;
		}
		for (substream = pcm->streams[cidx].substream; substream; substream = substream->next)
			snd_pcm_timer_init(substream);
	}
	list_for_each(list, &snd_pcm_notify_list) {
		snd_pcm_notify_t *notify;
		notify = list_entry(list, snd_pcm_notify_t, list);
		if (notify->n_register)
			notify->n_register(-1 /* idx + SNDRV_MINOR_PCM */, pcm);
	}
	snd_pcm_lock(1);
	return 0;
}

static int snd_pcm_dev_unregister(snd_device_t *device)
{
	int idx, cidx, devtype;
	snd_pcm_substream_t *substream;
	struct list_head *list;
	snd_pcm_t *pcm = snd_magic_cast(snd_pcm_t, device->device_data, return -ENXIO);

	snd_assert(pcm != NULL, return -ENXIO);
	snd_pcm_lock(0);
	idx = (pcm->card->number * SNDRV_PCM_DEVICES) + pcm->device;
	if (snd_pcm_devices[idx] != pcm) {
		snd_pcm_lock(1);
		return -EINVAL;
	}
	for (cidx = 0; cidx < 2; cidx++) {
		devtype = -1;
		switch (cidx) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			devtype = SNDRV_DEVICE_TYPE_PCM_PLAYBACK;
			break;
		case SNDRV_PCM_STREAM_CAPTURE:
			devtype = SNDRV_DEVICE_TYPE_PCM_CAPTURE;
			break;
		}
		snd_unregister_device(devtype, pcm->card, pcm->device);
		for (substream = pcm->streams[cidx].substream; substream; substream = substream->next)
			snd_pcm_timer_done(substream);
	}
	list_for_each(list, &snd_pcm_notify_list) {
		snd_pcm_notify_t *notify;
		notify = list_entry(list, snd_pcm_notify_t, list);
		if (notify->n_unregister)
			notify->n_unregister(-1 /* SNDRV_MINOR_PCM + idx */, pcm);
	}
	snd_pcm_devices[idx] = NULL;
	snd_pcm_lock(1);
	return snd_pcm_free(pcm);
}

int snd_pcm_notify(snd_pcm_notify_t *notify, int nfree)
{
	int idx;

	snd_assert(notify != NULL && notify->n_register != NULL && notify->n_unregister != NULL, return -EINVAL);
	snd_pcm_lock(0);
	if (nfree) {
		list_del(&notify->list);
		for (idx = 0; idx < SNDRV_CARDS * SNDRV_PCM_DEVICES; idx++) {
			if (snd_pcm_devices[idx] == NULL)
				continue;
			notify->n_unregister(-1 /* idx + SNDRV_MINOR_PCM */,
				             snd_pcm_devices[idx]);
		}
	} else {
		list_add_tail(&notify->list, &snd_pcm_notify_list);
		for (idx = 0; idx < SNDRV_CARDS * SNDRV_PCM_DEVICES; idx++) {
			if (snd_pcm_devices[idx] == NULL)
				continue;
			notify->n_register(-1 /* idx + SNDRV_MINOR_PCM */,
				           snd_pcm_devices[idx]);
		}
	}
	snd_pcm_lock(1);
	return 0;
}

/*
 *  Info interface
 */

static void snd_pcm_proc_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	int idx;
	snd_pcm_t *pcm;

	down(&register_mutex);
	for (idx = 0; idx < SNDRV_CARDS * SNDRV_PCM_DEVICES; idx++) {
		pcm = snd_pcm_devices[idx];
		if (pcm == NULL)
			continue;
		snd_iprintf(buffer, "%02i-%02i: %s : %s", idx / SNDRV_PCM_DEVICES,
			    idx % SNDRV_PCM_DEVICES, pcm->id, pcm->name);
		if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream)
			snd_iprintf(buffer, " : playback %i", pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream_count);
		if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream)
			snd_iprintf(buffer, " : capture %i", pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream_count);
		snd_iprintf(buffer, "\n");
	}
	up(&register_mutex);
}

/*
 *  ENTRY functions
 */

static snd_info_entry_t *snd_pcm_proc_entry = NULL;

static int __init alsa_pcm_init(void)
{
	snd_info_entry_t *entry;

	snd_ctl_register_ioctl(snd_pcm_control_ioctl);
	if ((entry = snd_info_create_module_entry(THIS_MODULE, "pcm", NULL)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->c.text.read_size = SNDRV_CARDS * SNDRV_PCM_DEVICES * 128;
		entry->c.text.read = snd_pcm_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_pcm_proc_entry = entry;
	return 0;
}

static void __exit alsa_pcm_exit(void)
{
	snd_ctl_unregister_ioctl(snd_pcm_control_ioctl);
	if (snd_pcm_proc_entry) {
		snd_info_unregister(snd_pcm_proc_entry);
		snd_pcm_proc_entry = NULL;
	}
}

module_init(alsa_pcm_init)
module_exit(alsa_pcm_exit)

EXPORT_SYMBOL(snd_pcm_lock);
EXPORT_SYMBOL(snd_pcm_devices);
EXPORT_SYMBOL(snd_pcm_new);
EXPORT_SYMBOL(snd_pcm_notify);
EXPORT_SYMBOL(snd_pcm_open_substream);
EXPORT_SYMBOL(snd_pcm_release_substream);
  /* pcm_native.c */
EXPORT_SYMBOL(snd_pcm_start);
EXPORT_SYMBOL(snd_pcm_kernel_playback_ioctl);
EXPORT_SYMBOL(snd_pcm_kernel_capture_ioctl);
EXPORT_SYMBOL(snd_pcm_kernel_ioctl);
EXPORT_SYMBOL(snd_pcm_open);
EXPORT_SYMBOL(snd_pcm_release);
EXPORT_SYMBOL(snd_pcm_playback_poll);
EXPORT_SYMBOL(snd_pcm_capture_poll);
EXPORT_SYMBOL(snd_pcm_mmap_data);
 /* pcm_misc.c */
EXPORT_SYMBOL(snd_pcm_format_signed);
EXPORT_SYMBOL(snd_pcm_format_unsigned);
EXPORT_SYMBOL(snd_pcm_format_linear);
EXPORT_SYMBOL(snd_pcm_format_little_endian);
EXPORT_SYMBOL(snd_pcm_format_big_endian);
EXPORT_SYMBOL(snd_pcm_format_width);
EXPORT_SYMBOL(snd_pcm_format_physical_width);
EXPORT_SYMBOL(snd_pcm_format_size);
EXPORT_SYMBOL(snd_pcm_format_silence_64);
EXPORT_SYMBOL(snd_pcm_format_set_silence);
EXPORT_SYMBOL(snd_pcm_build_linear_format);
