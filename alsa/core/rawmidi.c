/*
 *  Abstract layer for MIDI v1.0 stream
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
#include <sound/rawmidi.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/minors.h>
#include <sound/initval.h>

#ifdef CONFIG_SND_OSSEMUL
#ifdef TARGET_OS2
static int snd_midi_map[SNDRV_CARDS] = {REPEAT_SNDRV(0)};
static int snd_amidi_map[SNDRV_CARDS] = {REPEAT_SNDRV(1)};
#else
static int snd_midi_map[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = 0};
static int snd_amidi_map[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = 1};
#endif
MODULE_PARM(snd_midi_map, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_midi_map, "Raw MIDI device number assigned to 1st OSS device.");
MODULE_PARM_SYNTAX(snd_midi_map, "default:0,skill:advanced");
MODULE_PARM(snd_amidi_map, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_amidi_map, "Raw MIDI device number assigned to 2nd OSS device.");
MODULE_PARM_SYNTAX(snd_amidi_map, "default:1,skill:advanced");
#endif /* CONFIG_SND_OSSEMUL */

static int snd_rawmidi_free(snd_rawmidi_t *rawmidi);
static int snd_rawmidi_dev_free(snd_device_t *device);
static int snd_rawmidi_dev_register(snd_device_t *device);
static int snd_rawmidi_dev_unregister(snd_device_t *device);

snd_rawmidi_t *snd_rawmidi_devices[SNDRV_CARDS * SNDRV_RAWMIDI_DEVICES];

static DECLARE_MUTEX(register_mutex);

static inline unsigned short snd_rawmidi_file_flags(struct file *file)
{
	switch (file->f_mode & (FMODE_READ | FMODE_WRITE)) {
	case FMODE_WRITE:
		return SNDRV_RAWMIDI_LFLG_OUTPUT;
	case FMODE_READ:
		return SNDRV_RAWMIDI_LFLG_INPUT;
	default:
		return SNDRV_RAWMIDI_LFLG_OPEN;
	}
}

static inline int snd_rawmidi_ready(snd_rawmidi_substream_t * substream)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;
	return runtime->avail >= runtime->avail_min;
}

static int snd_rawmidi_init(snd_rawmidi_substream_t *substream)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;
	spin_lock_init(&runtime->lock);
	init_waitqueue_head(&runtime->sleep);
	runtime->event = NULL;
	runtime->buffer_size = PAGE_SIZE;
	runtime->avail_min = 1;
	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		runtime->avail = 0;
	else
		runtime->avail = runtime->buffer_size;
	if ((runtime->buffer = kmalloc(runtime->buffer_size, GFP_KERNEL)) == NULL)
		return -ENOMEM;
	runtime->appl_ptr = runtime->hw_ptr = 0;
	return 0;
}

static int snd_rawmidi_done_buffer(snd_rawmidi_runtime_t *runtime)
{
	if (runtime->buffer) {
		kfree(runtime->buffer);
		runtime->buffer = NULL;
	}
	return 0;
}

int snd_rawmidi_drop_output(snd_rawmidi_substream_t * substream)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	substream->ops->trigger(substream, 0);
	runtime->trigger = 0;
	runtime->drain = 0;
	/* interrupts are not enabled at this moment,
	   so spinlock is not required */
	runtime->appl_ptr = runtime->hw_ptr = 0;
	runtime->avail = runtime->buffer_size;
	return 0;
}

int snd_rawmidi_drain_output(snd_rawmidi_substream_t * substream)
{
	int err;
	long timeout;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	err = 0;
	runtime->drain = 1;
	while (runtime->avail < runtime->buffer_size) {
		timeout = interruptible_sleep_on_timeout(&runtime->sleep, 10 * HZ);
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
		if (runtime->avail < runtime->buffer_size && !timeout) {
			err = -EIO;
			break;
		}
	}
	runtime->drain = 0;
	if (err != -ERESTARTSYS) {
		/* we need wait a while to make sure that Tx FIFOs are empty */
		if (substream->ops->drain)
			substream->ops->drain(substream);
		else {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ / 20);
		}
		snd_rawmidi_drop_output(substream);
	}
	return err;
}

int snd_rawmidi_drain_input(snd_rawmidi_substream_t * substream)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	substream->ops->trigger(substream, 0);
	runtime->trigger = 0;
	runtime->drain = 0;
	/* interrupts aren't enabled at this moment, so spinlock isn't needed */
	runtime->appl_ptr = runtime->hw_ptr = 0;
	runtime->avail = 0;
	return 0;
}

int snd_rawmidi_kernel_open(int cardnum, int device, int subdevice,
			    int mode, snd_rawmidi_file_t * rfile)
{
	snd_rawmidi_t *rmidi;
	struct list_head *list1, *list2;
	snd_rawmidi_substream_t *sinput, *soutput;
	snd_rawmidi_runtime_t *input = NULL, *output = NULL;
	int err;

	if (rfile)
		rfile->input = rfile->output = NULL;
#ifndef LINUX_2_3
	MOD_INC_USE_COUNT;
#endif
	rmidi = snd_rawmidi_devices[(cardnum * SNDRV_RAWMIDI_DEVICES) + device];
	if (rmidi == NULL) {
		err = -ENODEV;
		goto __error1;
	}
	if (!try_inc_mod_count(rmidi->card->module)) {
		err = -EFAULT;
		goto __error1;
	}
	down(&rmidi->open_mutex);
	if (mode & SNDRV_RAWMIDI_LFLG_INPUT) {
		if (!(rmidi->info_flags & SNDRV_RAWMIDI_INFO_INPUT)) {
			err = -ENXIO;
			goto __error;
		}
		if (subdevice >= 0 && subdevice >= rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_count) {
			err = -ENODEV;
			goto __error;
		}
		if (rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_opened >=
		    rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_count) {
			err = -EAGAIN;
			goto __error;
		}
	}
	if (mode & SNDRV_RAWMIDI_LFLG_OUTPUT) {
		if (!(rmidi->info_flags & SNDRV_RAWMIDI_INFO_OUTPUT)) {
			err = -ENXIO;
			goto __error;
		}
		if (subdevice >= 0 && subdevice >= rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_count) {
			err = -ENODEV;
			goto __error;
		}
		if (rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_opened >=
		    rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_count) {
			err = -EAGAIN;
			goto __error;
		}
	}
	list1 = rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams.next;
	while (1) {
		if (list1 == &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams) {
			sinput = NULL;
			if (mode & SNDRV_RAWMIDI_LFLG_INPUT) {
				err = -EAGAIN;
				goto __error;
			}
			break;
		}
		sinput = list_entry(list1, snd_rawmidi_substream_t, list);
		if ((mode & SNDRV_RAWMIDI_LFLG_INPUT) && sinput->opened)
			goto __nexti;
		if (subdevice < 0 || (subdevice >= 0 && subdevice == sinput->number))
			break;
	      __nexti:
		list1 = list1->next;
	}
	list2 = rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams.next;
	while (1) {
		if (list2 == &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams) {
			soutput = NULL;
			if (mode & SNDRV_RAWMIDI_LFLG_OUTPUT) {
				err = -EAGAIN;
				goto __error;
			}
			break;
		}
		soutput = list_entry(list2, snd_rawmidi_substream_t, list);
		if (mode & SNDRV_RAWMIDI_LFLG_OUTPUT) {
			if (mode & SNDRV_RAWMIDI_LFLG_APPEND) {
				if (soutput->opened && !soutput->append)
					goto __nexto;
			} else {
				if (soutput->opened)
					goto __nexto;
			}
		}
		if (subdevice < 0 || (subdevice >= 0 && subdevice == soutput->number))
			break;
	      __nexto:
		list2 = list2->next;
	}
	if (mode & SNDRV_RAWMIDI_LFLG_INPUT) {
		input = snd_kcalloc(sizeof(snd_rawmidi_runtime_t), GFP_KERNEL);
		if (input == NULL) {
			err = -ENOMEM;
			goto __error;
		}
		sinput->runtime = input;
		if (snd_rawmidi_init(sinput) < 0) {
			err = -ENOMEM;
			goto __error;
		}
		if ((err = sinput->ops->open(sinput)) < 0) {
			sinput->runtime = NULL;
			goto __error;
		}
		sinput->opened = 1;
		rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_opened++;
	} else {
		sinput = NULL;
	}
	if (mode & SNDRV_RAWMIDI_LFLG_OUTPUT) {
		if (soutput->opened)
			goto __skip_output;
		output = snd_kcalloc(sizeof(snd_rawmidi_runtime_t), GFP_KERNEL);
		if (output == NULL) {
			err = -ENOMEM;
			goto __error;
		}
		soutput->runtime = output;
		if (snd_rawmidi_init(soutput) < 0) {
			if (mode & SNDRV_RAWMIDI_LFLG_INPUT)
				sinput->ops->close(sinput);
			err = -ENOMEM;
			goto __error;
		}
		soutput->runtime = output;
		if ((err = soutput->ops->open(soutput)) < 0) {
			if (mode & SNDRV_RAWMIDI_LFLG_INPUT) {
				sinput->ops->close(sinput);
				sinput->runtime = NULL;
			}
			soutput->runtime = NULL;
			goto __error;
		}
	      __skip_output:
		soutput->opened = 1;
		if (mode & SNDRV_RAWMIDI_LFLG_APPEND)
			soutput->append = 1;
	      	if (soutput->use_count++ == 0)
			soutput->active_sensing = 1;
		rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_opened++;
	} else {
		soutput = NULL;
	}
	up(&rmidi->open_mutex);
	if (rfile) {
		rfile->rmidi = rmidi;
		rfile->input = sinput;
		rfile->output = soutput;
	}
	return 0;

      __error:
	if (input != NULL)
		kfree(input);
	if (output != NULL)
		kfree(output);
	dec_mod_count(rmidi->card->module);
	up(&rmidi->open_mutex);
      __error1:
#ifndef LINUX_2_3
	MOD_DEC_USE_COUNT;
#endif
	return err;
}

static int snd_rawmidi_open(struct inode *inode, struct file *file)
{
	int major = MAJOR(inode->i_rdev);
	int cardnum;
	snd_card_t *card;
	int device, subdevice;
	unsigned short fflags;
	int err;
	snd_rawmidi_t *rmidi;
	snd_rawmidi_file_t *rawmidi_file;
	wait_queue_t wait;
	struct list_head *list;
	snd_ctl_file_t *kctl;

	switch (major) {
	case CONFIG_SND_MAJOR:
		cardnum = SNDRV_MINOR_CARD(MINOR(inode->i_rdev));
		device = SNDRV_MINOR_DEVICE(MINOR(inode->i_rdev)) - SNDRV_MINOR_RAWMIDI;
		break;
#ifdef CONFIG_SND_OSSEMUL
	case SOUND_MAJOR:
		cardnum = SNDRV_MINOR_OSS_CARD(MINOR(inode->i_rdev));
		device = SNDRV_MINOR_OSS_DEVICE(inode->i_rdev) == SNDRV_MINOR_OSS_MIDI ?
			snd_midi_map[cardnum] : snd_amidi_map[cardnum];
		break;
#endif
	default:
		return -ENXIO;
	}

	cardnum %= SNDRV_CARDS;
	device %= SNDRV_MINOR_RAWMIDIS;
	rmidi = snd_rawmidi_devices[(cardnum * SNDRV_RAWMIDI_DEVICES) + device];
	if (rmidi == NULL)
		return -ENODEV;
	card = rmidi->card;
#ifdef CONFIG_SND_OSSEMUL
	if (major == SOUND_MAJOR && !rmidi->ossreg)
		return -ENXIO;
#endif
	fflags = snd_rawmidi_file_flags(file);
	if ((file->f_flags & O_APPEND) && !(file->f_flags & O_NONBLOCK))
		return -EINVAL;		/* invalid combination */
	if ((file->f_flags & O_APPEND) || major != CONFIG_SND_MAJOR) /* OSS emul? */
		fflags |= SNDRV_RAWMIDI_LFLG_APPEND;
	rawmidi_file = snd_magic_kmalloc(snd_rawmidi_file_t, 0, GFP_KERNEL);
	if (rawmidi_file == NULL)
		return -ENOMEM;
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&rmidi->open_wait, &wait);
	while (1) {
		subdevice = -1;
		read_lock(&card->control_rwlock);
		list_for_each(list, &card->ctl_files) {
			kctl = snd_ctl_file(list);
			if (kctl->pid == current->pid) {
				subdevice = kctl->prefer_rawmidi_subdevice;
				break;
			}
		}
		read_unlock(&card->control_rwlock);
		err = snd_rawmidi_kernel_open(cardnum, device, subdevice, fflags, rawmidi_file);
		if (err >= 0)
			break;
		if (err == -EAGAIN) {
			if (file->f_flags & O_NONBLOCK) {
				err = -EBUSY;
				break;
			}
		} else
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&rmidi->open_wait, &wait);
	if (err >= 0) {
		file->private_data = rawmidi_file;
	} else {
		snd_magic_kfree(rawmidi_file);
	}
	return err;
}

int snd_rawmidi_kernel_release(snd_rawmidi_file_t * rfile)
{
	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *substream;
	snd_rawmidi_runtime_t *runtime;

	snd_assert(rfile != NULL, return -ENXIO);
	snd_assert(rfile->input != NULL || rfile->output != NULL, return -ENXIO);
	rmidi = rfile->rmidi;
	down(&rmidi->open_mutex);
	if (rfile->input != NULL) {
		substream = rfile->input;
		rfile->input = NULL;
		runtime = substream->runtime;
		runtime->trigger = 0;
		substream->ops->trigger(substream, 0);
		substream->ops->close(substream);
		snd_rawmidi_done_buffer(runtime);
		if (runtime->private_free != NULL)
			runtime->private_free(substream);
		kfree(runtime);
		substream->runtime = NULL;
		substream->opened = 0;
		rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_opened--;
	}
	if (rfile->output != NULL) {
		substream = rfile->output;
		rfile->output = NULL;
		if (--substream->use_count == 0) {
			runtime = substream->runtime;
			if (substream->active_sensing) {
				unsigned char buf = 0xfe;
				/* sending single active sensing message to shut the device up */
				snd_rawmidi_kernel_write(substream, &buf, 1);
			}
			if (snd_rawmidi_drain_output(substream) == -ERESTARTSYS)
				substream->ops->trigger(substream, 0);
			substream->ops->close(substream);
			snd_rawmidi_done_buffer(runtime);
			if (runtime->private_free != NULL)
				runtime->private_free(substream);
			kfree(runtime);
			substream->runtime = NULL;
			substream->opened = 0;
			substream->append = 0;
		}
		rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_opened--;
	}
	up(&rmidi->open_mutex);
	dec_mod_count(rmidi->card->module);
#ifndef LINUX_2_3
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}

static int snd_rawmidi_release(struct inode *inode, struct file *file)
{
	snd_rawmidi_file_t *rfile;
	int err;

	rfile = snd_magic_cast(snd_rawmidi_file_t, file->private_data, return -ENXIO);
	err = snd_rawmidi_kernel_release(rfile);
	wake_up(&rfile->rmidi->open_wait);
	snd_magic_kfree(rfile);
	return err;
}

int snd_rawmidi_info(snd_rawmidi_substream_t *substream, snd_rawmidi_info_t *info)
{
	snd_rawmidi_t *rmidi = substream->rmidi;
	memset(info, 0, sizeof(*info));
	info->card = rmidi->card->number;
	info->device = rmidi->device;
	info->subdevice = substream->number;
	info->stream = substream->stream;
	info->flags = rmidi->info_flags;
	strcpy(info->id, rmidi->id);
	strcpy(info->name, rmidi->name);
	strcpy(info->subname, substream->name);
	info->subdevices_count = substream->pstr->substream_count;
	info->subdevices_avail = (substream->pstr->substream_count -
				  substream->pstr->substream_opened);
	return 0;
}

static int snd_rawmidi_info_user(snd_rawmidi_substream_t *substream, snd_rawmidi_info_t * _info)
{
	snd_rawmidi_info_t info;
	int err;
	if ((err = snd_rawmidi_info(substream, &info)) < 0)
		return err;
	if (copy_to_user(_info, &info, sizeof(snd_rawmidi_info_t)))
		return -EFAULT;
	return 0;
}

int snd_rawmidi_info_select(snd_card_t *card, snd_rawmidi_info_t *info)
{
	snd_rawmidi_t *rmidi;
	snd_rawmidi_str_t *pstr;
	snd_rawmidi_substream_t *substream;
	struct list_head *list;
	if (info->device >= SNDRV_RAWMIDI_DEVICES)
		return -ENXIO;
	rmidi = snd_rawmidi_devices[card->number * SNDRV_RAWMIDI_DEVICES + info->device];
	if (info->stream < 0 || info->stream > 1)
		return -EINVAL;
	pstr = &rmidi->streams[info->stream];
	if (pstr->substream_count == 0)
		return -ENOENT;
	if (info->subdevice >= pstr->substream_count)
		return -ENXIO;
	list_for_each(list, &pstr->substreams) {
		substream = list_entry(list, snd_rawmidi_substream_t, list);
		if (substream->number == info->subdevice)
			return snd_rawmidi_info(substream, info);
	}
	return -ENXIO;
}

static int snd_rawmidi_info_select_user(snd_card_t *card,
					snd_rawmidi_info_t *_info)
{
	int err;
	snd_rawmidi_info_t info;
	if (get_user(info.device, &_info->device))
		return -EFAULT;
	if (get_user(info.stream, &_info->stream))
		return -EFAULT;
	if (get_user(info.subdevice, &_info->subdevice))
		return -EFAULT;
	if ((err = snd_rawmidi_info_select(card, &info)) < 0)
		return err;
	if (copy_to_user(_info, &info, sizeof(snd_rawmidi_info_t)))
		return -EFAULT;
	return 0;
}

int snd_rawmidi_output_params(snd_rawmidi_substream_t * substream,
			      snd_rawmidi_params_t * params)
{
	char *newbuf;
	snd_rawmidi_runtime_t *runtime = substream->runtime;
	
	if (substream->append && substream->use_count > 1)
		return -EBUSY;
	snd_rawmidi_drain_output(substream);
	if (params->buffer_size < 32 || params->buffer_size > 1024L * 1024L) {
		return -EINVAL;
	}
	if (params->avail_min < 1 || params->avail_min >= params->buffer_size) {
		return -EINVAL;
	}
	if (params->buffer_size != runtime->buffer_size) {
		if ((newbuf = (char *) kmalloc(params->buffer_size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		kfree(runtime->buffer);
		runtime->buffer = newbuf;
		runtime->buffer_size = params->buffer_size;
	}
	runtime->avail_min = params->avail_min;
	substream->active_sensing = !params->no_active_sensing;
	return 0;
}

int snd_rawmidi_input_params(snd_rawmidi_substream_t * substream,
			     snd_rawmidi_params_t * params)
{
	char *newbuf;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	snd_rawmidi_drain_input(substream);
	if (params->buffer_size < 32 || params->buffer_size > 1024L * 1024L) {
		return -EINVAL;
	}
	if (params->avail_min < 1 || params->avail_min >= params->buffer_size) {
		return -EINVAL;
	}
	if (params->buffer_size != runtime->buffer_size) {
		if ((newbuf = (char *) kmalloc(params->buffer_size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		kfree(runtime->buffer);
		runtime->buffer = newbuf;
		runtime->buffer_size = params->buffer_size;
	}
	runtime->avail_min = params->avail_min;
	return 0;
}

static int snd_rawmidi_output_status(snd_rawmidi_substream_t * substream,
				     snd_rawmidi_status_t * status)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	memset(status, 0, sizeof(*status));
	status->stream = SNDRV_RAWMIDI_STREAM_OUTPUT;
	spin_lock_irq(&runtime->lock);
	status->avail = runtime->avail;
	spin_unlock_irq(&runtime->lock);
	return 0;
}

static int snd_rawmidi_input_status(snd_rawmidi_substream_t * substream,
				    snd_rawmidi_status_t * status)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	memset(status, 0, sizeof(*status));
	status->stream = SNDRV_RAWMIDI_STREAM_INPUT;
	spin_lock_irq(&runtime->lock);
	status->avail = runtime->avail;
	status->xruns = runtime->xruns;
	runtime->xruns = 0;
	spin_unlock_irq(&runtime->lock);
	return 0;
}

static int snd_rawmidi_ioctl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	snd_rawmidi_file_t *rfile;

	rfile = snd_magic_cast(snd_rawmidi_file_t, file->private_data, return -ENXIO);
	if (((cmd >> 8) & 0xff) != 'W')
		return -ENOTTY;
	switch (cmd) {
	case SNDRV_RAWMIDI_IOCTL_PVERSION:
		return put_user(SNDRV_RAWMIDI_VERSION, (int *)arg) ? -EFAULT : 0;
	case SNDRV_RAWMIDI_IOCTL_INFO:
	{
		snd_rawmidi_stream_t stream;
		snd_rawmidi_info_t *info = (snd_rawmidi_info_t *) arg;
		if (get_user(stream, &info->stream))
			return -EFAULT;
		switch (stream) {
		case SNDRV_RAWMIDI_STREAM_INPUT:
			return snd_rawmidi_info_user(rfile->input, info);
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			return snd_rawmidi_info_user(rfile->output, info);
		default:
			return -EINVAL;
		}
	}
	case SNDRV_RAWMIDI_IOCTL_PARAMS:
	{
		snd_rawmidi_params_t params;
		int err;
		if (copy_from_user(&params, (snd_rawmidi_params_t *) arg, sizeof(snd_rawmidi_params_t)))
			return -EFAULT;
		switch (params.stream) {
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			if (rfile->output == NULL)
				return -EINVAL;
			return snd_rawmidi_output_params(rfile->output, &params);
		case SNDRV_RAWMIDI_STREAM_INPUT:
			if (rfile->input == NULL)
				return -EINVAL;
			return snd_rawmidi_input_params(rfile->input, &params);
		default:
			return -EINVAL;
		}
		if (copy_to_user((snd_rawmidi_params_t *) arg, &params, sizeof(snd_rawmidi_params_t)))
			return -EFAULT;
		return err;
	}
	case SNDRV_RAWMIDI_IOCTL_STATUS:
	{
		int err = 0;
		snd_rawmidi_status_t status;
		if (copy_from_user(&status, (snd_rawmidi_status_t *) arg, sizeof(snd_rawmidi_status_t)))
			return -EFAULT;
		switch (status.stream) {
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			if (rfile->output == NULL)
				return -EINVAL;
			err = snd_rawmidi_output_status(rfile->output, &status);
			break;
		case SNDRV_RAWMIDI_STREAM_INPUT:
			if (rfile->input == NULL)
				return -EINVAL;
			err = snd_rawmidi_input_status(rfile->input, &status);
			break;
		default:
			return -EINVAL;
		}
		if (err < 0)
			return err;
		if (copy_to_user((snd_rawmidi_status_t *) arg, &status, sizeof(snd_rawmidi_status_t)))
			return -EFAULT;
		return 0;
	}
	case SNDRV_RAWMIDI_IOCTL_DROP:
	{
		int val;
		if (get_user(val, (long *) arg))
			return -EFAULT;
		switch (val) {
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			if (rfile->output == NULL)
				return -EINVAL;
			return snd_rawmidi_drop_output(rfile->output);
		default:
			return -EINVAL;
		}
	}
	case SNDRV_RAWMIDI_IOCTL_DRAIN:
	{
		int val;
		if (get_user(val, (long *) arg))
			return -EFAULT;
		switch (val) {
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			if (rfile->output == NULL)
				return -EINVAL;
			return snd_rawmidi_drain_output(rfile->output);
		case SNDRV_RAWMIDI_STREAM_INPUT:
			if (rfile->input == NULL)
				return -EINVAL;
			return snd_rawmidi_drain_input(rfile->input);
		default:
			return -EINVAL;
		}
	}
#ifdef CONFIG_SND_DEBUG
	default:
		snd_printk("rawmidi: unknown command = 0x%x\n", cmd);
#endif
	}
	return -ENOTTY;
}

int snd_rawmidi_control_ioctl(snd_card_t * card, snd_ctl_file_t * control,
			      unsigned int cmd, unsigned long arg)
{
	unsigned int tmp;

	tmp = card->number * SNDRV_RAWMIDI_DEVICES;
	switch (cmd) {
	case SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE:
	{
		int device;
		
		if (get_user(device, (int *)arg))
			return -EFAULT;
		device = device < 0 ? 0 : device + 1;
		while (device < SNDRV_RAWMIDI_DEVICES) {
			if (snd_rawmidi_devices[tmp + device])
				break;
			device++;
		}
		if (device == SNDRV_RAWMIDI_DEVICES)
			device = -1;
		if (put_user(device, (int *)arg))
			return -EFAULT;
		return 0;
	}
	case SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE:
	{
		int val;
		
		if (get_user(val, (int *)arg))
			return -EFAULT;
		control->prefer_rawmidi_subdevice = val;
		return 0;
	}
	case SNDRV_CTL_IOCTL_RAWMIDI_INFO:
		return snd_rawmidi_info_select_user(card, (snd_rawmidi_info_t *)arg);
	}
	return -ENOIOCTLCMD;
}

void snd_rawmidi_receive_reset(snd_rawmidi_substream_t * substream)
{
	/* TODO: reset current state */
}

int snd_rawmidi_receive(snd_rawmidi_substream_t * substream, unsigned char *buffer, int count)
{
	unsigned long flags;
	int result = 0, count1;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	if (runtime->buffer == NULL) {
		snd_printd("snd_rawmidi_receive: input is not active!!!\n");
		return -EINVAL;
	}
	spin_lock_irqsave(&runtime->lock, flags);
	if (count == 1) {	/* special case, faster code */
		substream->bytes++;
		if (runtime->avail < runtime->buffer_size) {
			runtime->buffer[runtime->hw_ptr++] = buffer[0];
			runtime->hw_ptr %= runtime->buffer_size;
			runtime->avail++;
			result++;
		} else {
			runtime->xruns++;
		}
	} else {
		substream->bytes += count;
		count1 = runtime->buffer_size - runtime->hw_ptr;
		if (count1 > count)
			count1 = count;
		if (count1 > runtime->buffer_size - runtime->avail)
			count1 = runtime->buffer_size - runtime->avail;
		memcpy(runtime->buffer + runtime->hw_ptr, buffer, count1);
		runtime->hw_ptr += count1;
		runtime->hw_ptr %= runtime->buffer_size;
		runtime->avail += count1;
		count -= count1;
		result += count1;
		if (count > 0) {
			buffer += count1;
			count1 = count;
			if (count1 > runtime->buffer_size - runtime->avail) {
				count1 = runtime->buffer_size - runtime->avail;
				runtime->xruns = count - count1;
			}
			if (count1 > 0) {
				memcpy(runtime->buffer, buffer, count1);
				runtime->hw_ptr = count1;
				runtime->avail += count1;
				result += count1;
			}
		}
	}
	if (result > 0 && runtime->event == NULL) {
		if (snd_rawmidi_ready(substream))
			wake_up(&runtime->sleep);
	}
	spin_unlock_irqrestore(&runtime->lock, flags);
	if (result > 0 && runtime->event)
		runtime->event(substream);
	return result;
}

static long snd_rawmidi_kernel_read1(snd_rawmidi_substream_t *substream,
				     unsigned char *buf, long count, int kernel)
{
	unsigned long flags;
	long result = 0, count1;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	while (count > 0 && runtime->avail) {
		count1 = runtime->buffer_size - runtime->appl_ptr;
		if (count1 > count)
			count1 = count;
		spin_lock_irqsave(&runtime->lock, flags);
		if (count1 > runtime->avail)
			count1 = runtime->avail;
		if (kernel) {
			memcpy(buf + result, runtime->buffer + runtime->appl_ptr, count1);
		} else {
			if (copy_to_user(buf + result, runtime->buffer + runtime->appl_ptr, count1)) {
				spin_unlock_irqrestore(&runtime->lock, flags);
				return result > 0 ? result : -EFAULT;
			}
		}
		runtime->appl_ptr += count1;
		runtime->appl_ptr %= runtime->buffer_size;
		runtime->avail -= count1;
		spin_unlock_irqrestore(&runtime->lock, flags);
		result += count1;
		count -= count1;
	}
	return result;
}

long snd_rawmidi_kernel_read(snd_rawmidi_substream_t *substream, unsigned char *buf, long count)
{
	substream->runtime->trigger = 1;
	substream->ops->trigger(substream, 1);
	return snd_rawmidi_kernel_read1(substream, buf, count, 1);
}

static ssize_t snd_rawmidi_read(struct file *file, char *buf, size_t count, loff_t *offset)
{
	long result;
	int count1;
	snd_rawmidi_file_t *rfile;
	snd_rawmidi_substream_t *substream;
	snd_rawmidi_runtime_t *runtime;

	rfile = snd_magic_cast(snd_rawmidi_file_t, file->private_data, return -ENXIO);
	substream = rfile->input;
	if (substream == NULL)
		return -EIO;
	runtime = substream->runtime;
	runtime->trigger = 1;
	substream->ops->trigger(substream, 1);
	result = 0;
	while (count > 0) {
		spin_lock_irq(&runtime->lock);
		while (!snd_rawmidi_ready(substream)) {
			wait_queue_t wait;
			if (file->f_flags & O_NONBLOCK) {
				spin_unlock_irq(&runtime->lock);
				return result > 0 ? result : -EAGAIN;
			}
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&runtime->sleep, &wait);
			spin_unlock_irq(&runtime->lock);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&runtime->sleep, &wait);
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			if (!runtime->avail)
				return result > 0 ? result : -EIO;
			spin_lock_irq(&runtime->lock);
		}
		spin_unlock_irq(&runtime->lock);
		count1 = snd_rawmidi_kernel_read1(substream, buf, count, 0);
		result += count1;
		buf += count1;
		count -= count1;
	}
	return result;
}

void snd_rawmidi_transmit_reset(snd_rawmidi_substream_t * substream)
{
	/* TODO: reset current state */
}

int snd_rawmidi_transmit_empty(snd_rawmidi_substream_t * substream)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;
	int result;
	unsigned long flags;

	if (runtime->buffer == NULL) {
		snd_printd("snd_rawmidi_transmit_empty: output is not active!!!\n");
		return 1;
	}
	spin_lock_irqsave(&runtime->lock, flags);
	result = runtime->avail >= runtime->buffer_size;
	if (result)
		runtime->trigger = 1;
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;		
}

int snd_rawmidi_transmit_peek(snd_rawmidi_substream_t * substream, unsigned char *buffer, int count)
{
	unsigned long flags;
	int result, count1;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	if (runtime->buffer == NULL) {
		snd_printd("snd_rawmidi_transmit_peek: output is not active!!!\n");
		return -EINVAL;
	}
	result = 0;
	spin_lock_irqsave(&runtime->lock, flags);
	if (runtime->avail >= runtime->buffer_size) {
		/* warning: lowlevel layer MUST trigger down the hardware */
		runtime->trigger = 0;
		goto __skip;
	}
	if (count == 1) {	/* special case, faster code */
		*buffer = runtime->buffer[runtime->hw_ptr];
		result++;
	} else {
		count1 = runtime->buffer_size - runtime->hw_ptr;
		if (count1 > count)
			count1 = count;
		if (count1 > runtime->buffer_size - runtime->avail)
			count1 = runtime->buffer_size - runtime->avail;
		memcpy(buffer, runtime->buffer + runtime->hw_ptr, count1);
		count -= count1;
		result += count1;
		if (count > 0)
			memcpy(buffer + count1, runtime->buffer, count);
	}
      __skip:
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;
}

int snd_rawmidi_transmit_ack(snd_rawmidi_substream_t * substream, int count)
{
	unsigned long flags;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	if (runtime->buffer == NULL) {
		snd_printd("snd_rawmidi_transmit_ack: output is not active!!!\n");
		return -EINVAL;
	}
	spin_lock_irqsave(&runtime->lock, flags);
	snd_assert(runtime->avail + count <= runtime->buffer_size, );
	runtime->hw_ptr += count;
	runtime->hw_ptr %= runtime->buffer_size;
	runtime->avail += count;
	substream->bytes += count;
	if (runtime->drain)
		wake_up(&runtime->sleep);
	else
		if (count > 0 && runtime->event == NULL)
			if (snd_rawmidi_ready(substream))
				wake_up(&runtime->sleep);
	spin_unlock_irqrestore(&runtime->lock, flags);
	if (count > 0 && runtime->event)
		runtime->event(substream);
	return count;
}

int snd_rawmidi_transmit(snd_rawmidi_substream_t * substream, unsigned char *buffer, int count)
{
	count = snd_rawmidi_transmit_peek(substream, buffer, count);
	if (count < 0)
		return count;
	return snd_rawmidi_transmit_ack(substream, count);
}

static long snd_rawmidi_kernel_write1(snd_rawmidi_substream_t * substream, const unsigned char *buf, long count, int kernel)
{
	unsigned long flags;
	long count1, result;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	snd_assert(buf != NULL, return -EINVAL);
	snd_assert(runtime->buffer != NULL, return -EINVAL);

	result = 0;
	spin_lock_irqsave(&runtime->lock, flags);
	if (substream->append) {
		if (runtime->avail < count) {
			spin_unlock_irqrestore(&runtime->lock, flags);
			return -EAGAIN;
		}
	}
	while (count > 0 && runtime->avail) {
		count1 = runtime->buffer_size - runtime->appl_ptr;
		if (count1 > count)
			count1 = count;
		if (count1 > runtime->avail)
			count1 = runtime->avail;
		if (kernel) {
			memcpy(runtime->buffer + runtime->appl_ptr, buf, count1);
		} else {
			if (copy_from_user(runtime->buffer + runtime->appl_ptr, buf, count1)) {
				result = result > 0 ? result : -EFAULT;
				goto __end;
			}
		}
		runtime->appl_ptr += count1;
		runtime->appl_ptr %= runtime->buffer_size;
		runtime->avail -= count1;
		result += count1;
		buf += count1;
		count -= count1;
	}
      __end:
	if (result > 0)
		runtime->trigger = 1;
	spin_unlock_irqrestore(&runtime->lock, flags);
	substream->ops->trigger(substream, 1);
	return result;
}

long snd_rawmidi_kernel_write(snd_rawmidi_substream_t * substream, const unsigned char *buf, long count)
{
	return snd_rawmidi_kernel_write1(substream, buf, count, 1);
}

static ssize_t snd_rawmidi_write(struct file *file, const char *buf, size_t count, loff_t *offset)
{
	long result, timeout;
	int count1;
	snd_rawmidi_file_t *rfile;
	snd_rawmidi_runtime_t *runtime;
	snd_rawmidi_substream_t *substream;

	rfile = snd_magic_cast(snd_rawmidi_file_t, file->private_data, return -ENXIO);
	substream = rfile->output;
	runtime = substream->runtime;
	result = 0;
	while (count > 0) {
		spin_lock_irq(&runtime->lock);
		while (!snd_rawmidi_ready(substream)) {
			wait_queue_t wait;
			if (file->f_flags & O_NONBLOCK) {
				spin_unlock_irq(&runtime->lock);
				return result > 0 ? result : -EAGAIN;
			}
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&runtime->sleep, &wait);
			spin_unlock_irq(&runtime->lock);
			set_current_state(TASK_INTERRUPTIBLE);
			timeout = schedule_timeout(30 * HZ);
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&runtime->sleep, &wait);
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			if (!runtime->avail && !timeout)
				return result > 0 ? result : -EIO;
			spin_lock_irq(&runtime->lock);
		}
		spin_unlock_irq(&runtime->lock);
		count1 = snd_rawmidi_kernel_write1(substream, buf, count, 0);
		result += count1;
		buf += count1;
		count -= count1;
		if (count1 < count && (file->f_flags & O_NONBLOCK))
			break;
	}
	while (file->f_flags & O_SYNC) {
		spin_lock_irq(&runtime->lock);
		while (runtime->avail != runtime->buffer_size) {
			wait_queue_t wait;
			unsigned int last_avail = runtime->avail;
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&runtime->sleep, &wait);
			spin_unlock_irq(&runtime->lock);
			set_current_state(TASK_INTERRUPTIBLE);
			timeout = schedule_timeout(30 * HZ);
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&runtime->sleep, &wait);
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			if (runtime->avail == last_avail && !timeout)
				return result > 0 ? result : -EIO;
			spin_lock_irq(&runtime->lock);
		}
		spin_unlock_irq(&runtime->lock);
	}
	return result;
}

static unsigned int snd_rawmidi_poll(struct file *file, poll_table * wait)
{
	snd_rawmidi_file_t *rfile;
	snd_rawmidi_runtime_t *runtime;
	unsigned int mask;

	rfile = snd_magic_cast(snd_rawmidi_file_t, file->private_data, return 0);
	if (rfile->input != NULL) {
		runtime = rfile->input->runtime;
		runtime->trigger = 1;
		rfile->input->ops->trigger(rfile->input, 1);
		poll_wait(file, &runtime->sleep, wait);
	}
	if (rfile->output != NULL) {
		runtime = rfile->output->runtime;
		poll_wait(file, &runtime->sleep, wait);
	}
	mask = 0;
	if (rfile->input != NULL) {
		if (snd_rawmidi_ready(rfile->input))
			mask |= POLLIN | POLLRDNORM;
	}
	if (rfile->output != NULL) {
		if (snd_rawmidi_ready(rfile->output))
			mask |= POLLOUT | POLLWRNORM;
	}
	return mask;
}

/*

 */

static void snd_rawmidi_proc_info_read(snd_info_entry_t *entry,
				       snd_info_buffer_t * buffer)
{
	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *substream;
	snd_rawmidi_runtime_t *runtime;
	struct list_head *list;

	rmidi = snd_magic_cast(snd_rawmidi_t, entry->private_data, return);
	snd_iprintf(buffer, "%s\n\n", rmidi->name);
	down(&rmidi->open_mutex);
	if (rmidi->info_flags & SNDRV_RAWMIDI_INFO_OUTPUT) {
		list_for_each(list, &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams) {
			substream = list_entry(list, snd_rawmidi_substream_t, list);
			snd_iprintf(buffer,
				    "Output %d\n"
				    "  Tx bytes     : %lu\n",
				    substream->number,
				    (unsigned long) substream->bytes);
			if (substream->opened) {
				runtime = substream->runtime;
				snd_iprintf(buffer,
				    "  Mode         : %s\n"
				    "  Buffer size  : %lu\n"
				    "  Avail        : %lu\n",
				    runtime->oss ? "OSS compatible" : "native",
				    (unsigned long) runtime->buffer_size,
				    (unsigned long) runtime->avail);
			}
		}
	}
	if (rmidi->info_flags & SNDRV_RAWMIDI_INFO_INPUT) {
		list_for_each(list, &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams) {
			substream = list_entry(list, snd_rawmidi_substream_t, list);
			snd_iprintf(buffer,
				    "Input %d\n"
				    "  Rx bytes     : %lu\n",
				    substream->number,
				    (unsigned long) substream->bytes);
			if (substream->opened) {
				runtime = substream->runtime;
				snd_iprintf(buffer,
					    "  Buffer size  : %lu\n"
					    "  Avail        : %lu\n"
					    "  Overruns     : %lu\n",
					    (unsigned long) runtime->buffer_size,
					    (unsigned long) runtime->avail,
					    (unsigned long) runtime->xruns);
			}
		}
	}
	up(&rmidi->open_mutex);
}

/*
 *  Register functions
 */

#ifdef TARGET_OS2
static struct file_operations snd_rawmidi_f_ops =
{
#ifdef LINUX_2_3
	THIS_MODULE,
#endif
        0,
	snd_rawmidi_read,
	snd_rawmidi_write,
        0,
	snd_rawmidi_poll,
	snd_rawmidi_ioctl,
        0,
	snd_rawmidi_open,
        0,
	snd_rawmidi_release,
        0,0,0,0,0
};

static snd_minor_t snd_rawmidi_reg =
{
        {0,0},
        0,0,
	"raw midi",0,
	&snd_rawmidi_f_ops,
};
#else
static struct file_operations snd_rawmidi_f_ops =
{
#ifdef LINUX_2_3
	owner:		THIS_MODULE,
#endif
	read:		snd_rawmidi_read,
	write:		snd_rawmidi_write,
	open:		snd_rawmidi_open,
	release:	snd_rawmidi_release,
	poll:		snd_rawmidi_poll,
	ioctl:		snd_rawmidi_ioctl,
};

static snd_minor_t snd_rawmidi_reg =
{
	comment:	"raw midi",
	f_ops:		&snd_rawmidi_f_ops,
};
#endif

static int snd_rawmidi_alloc_substreams(snd_rawmidi_t *rmidi,
					snd_rawmidi_str_t *stream,
					int direction,
					int count)
{
	snd_rawmidi_substream_t *substream;
	int idx;

	INIT_LIST_HEAD(&stream->substreams);
	for (idx = 0; idx < count; idx++) {
		substream = snd_kcalloc(sizeof(snd_rawmidi_substream_t), GFP_KERNEL);
		if (substream == NULL)
			return -ENOMEM;
		substream->stream = direction;
		substream->number = idx;
		substream->rmidi = rmidi;
		substream->pstr = stream;
		list_add_tail(&substream->list, &stream->substreams);
		stream->substream_count++;
	}
	return 0;
}

int snd_rawmidi_new(snd_card_t * card, char *id, int device,
		    int output_count, int input_count,
		    snd_rawmidi_t ** rrawmidi)
{
	snd_rawmidi_t *rmidi;
	int err;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_rawmidi_dev_free,
		snd_rawmidi_dev_register,
		snd_rawmidi_dev_unregister
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_rawmidi_dev_free,
		dev_register:	snd_rawmidi_dev_register,
		dev_unregister:	snd_rawmidi_dev_unregister
	};
#endif

	snd_assert(rrawmidi != NULL, return -EINVAL);
	*rrawmidi = NULL;
	snd_assert(card != NULL, return -ENXIO);
	rmidi = snd_magic_kcalloc(snd_rawmidi_t, 0, GFP_KERNEL);
	if (rmidi == NULL)
		return -ENOMEM;
	rmidi->card = card;
	rmidi->device = device;
	init_MUTEX(&rmidi->open_mutex);
	init_waitqueue_head(&rmidi->open_wait);
	if (id != NULL)
		strncpy(rmidi->id, id, sizeof(rmidi->id) - 1);
	if ((err = snd_rawmidi_alloc_substreams(rmidi, &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT], SNDRV_RAWMIDI_STREAM_INPUT, input_count)) < 0) {
		snd_rawmidi_free(rmidi);
		return err;
	}
	if ((err = snd_rawmidi_alloc_substreams(rmidi, &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT], SNDRV_RAWMIDI_STREAM_OUTPUT, output_count)) < 0) {
		snd_rawmidi_free(rmidi);
		return err;
	}
	if ((err = snd_device_new(card, SNDRV_DEV_RAWMIDI, rmidi, &ops)) < 0) {
		snd_rawmidi_free(rmidi);
		return err;
	}
	*rrawmidi = rmidi;
	return 0;
}

static void snd_rawmidi_free_substreams(snd_rawmidi_str_t *stream)
{
	snd_rawmidi_substream_t *substream;

	while (!list_empty(&stream->substreams)) {
		substream = list_entry(stream->substreams.next, snd_rawmidi_substream_t, list);
		list_del(&substream->list);
		kfree(substream);
	}
}

static int snd_rawmidi_free(snd_rawmidi_t *rmidi)
{
	snd_assert(rmidi != NULL, return -ENXIO);	
	snd_rawmidi_free_substreams(&rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT]);
	snd_rawmidi_free_substreams(&rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT]);
	if (rmidi->private_free)
		rmidi->private_free(rmidi);
	snd_magic_kfree(rmidi);
	return 0;
}

static int snd_rawmidi_dev_free(snd_device_t *device)
{
	snd_rawmidi_t *rmidi = snd_magic_cast(snd_rawmidi_t, device->device_data, return -ENXIO);
	return snd_rawmidi_free(rmidi);
}

#ifdef CONFIG_SND_SEQUENCER
static void snd_rawmidi_dev_seq_free(snd_seq_device_t *device)
{
	snd_rawmidi_t *rmidi = snd_magic_cast(snd_rawmidi_t, device->private_data, return);
	rmidi->seq_dev = NULL;
}
#endif

static int snd_rawmidi_dev_register(snd_device_t *device)
{
	int idx, err;
	snd_info_entry_t *entry;
	char name[16];
	snd_rawmidi_t *rmidi = snd_magic_cast(snd_rawmidi_t, device->device_data, return -ENXIO);

	if (rmidi->device >= SNDRV_RAWMIDI_DEVICES)
		return -ENOMEM;
	down(&register_mutex);
	idx = (rmidi->card->number * SNDRV_RAWMIDI_DEVICES) + rmidi->device;
	if (snd_rawmidi_devices[idx] != NULL) {
		up(&register_mutex);
		return -EBUSY;
	}
	snd_rawmidi_devices[idx] = rmidi;
	sprintf(name, "midiC%iD%i", rmidi->card->number, rmidi->device);
	if ((err = snd_register_device(SNDRV_DEVICE_TYPE_RAWMIDI,
				       rmidi->card, rmidi->device,
				       &snd_rawmidi_reg, name)) < 0) {
		snd_printk("unable to register rawmidi device %i:%i\n", rmidi->card->number, rmidi->device);
		snd_rawmidi_devices[idx] = NULL;
		up(&register_mutex);
		return err;
	}
	if (rmidi->ops && rmidi->ops->dev_register &&
	    (err = rmidi->ops->dev_register(rmidi)) < 0) {
		snd_unregister_device(SNDRV_DEVICE_TYPE_RAWMIDI, rmidi->card, rmidi->device);
		snd_rawmidi_devices[idx] = NULL;
		up(&register_mutex);
		return err;
	}
#ifdef CONFIG_SND_OSSEMUL
	rmidi->ossreg = 0;
	if (rmidi->device == snd_midi_map[rmidi->card->number]) {
		if (snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI,
					    rmidi->card, 0, &snd_rawmidi_reg, name) < 0) {
			snd_printk("unable to register OSS rawmidi device %i:%i\n", rmidi->card->number, 0);
		} else {
			rmidi->ossreg++;
			snd_oss_info_register(SNDRV_OSS_INFO_DEV_MIDI, rmidi->card->number, rmidi->name);
		}
	}
	if (rmidi->device == snd_amidi_map[rmidi->card->number]) {
		if (snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI,
					    rmidi->card, 1, &snd_rawmidi_reg, name) < 0) {
			snd_printk("unable to register OSS rawmidi device %i:%i\n", rmidi->card->number, 1);
		} else {
			rmidi->ossreg++;
		}
	}
#endif
	up(&register_mutex);
	sprintf(name, "midi%d", rmidi->device);
	entry = snd_info_create_card_entry(rmidi->card, name, rmidi->card->proc_root);
	if (entry) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = rmidi;
		entry->c.text.read_size = 1024;
		entry->c.text.read = snd_rawmidi_proc_info_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	rmidi->proc_entry = entry;
#ifdef CONFIG_SND_SEQUENCER
	if (!rmidi->ops || !rmidi->ops->dev_register) { /* own registration mechanism */
		if (snd_seq_device_new(rmidi->card, rmidi->device, SNDRV_SEQ_DEV_ID_MIDISYNTH, 0, &rmidi->seq_dev) >= 0) {
			rmidi->seq_dev->private_data = rmidi;
			rmidi->seq_dev->private_free = snd_rawmidi_dev_seq_free;
			sprintf(rmidi->seq_dev->name, "MIDI %d-%d", rmidi->card->number, rmidi->device);
			snd_device_register(rmidi->card, rmidi->seq_dev);
		}
	}
#endif
	return 0;
}

static int snd_rawmidi_dev_unregister(snd_device_t *device)
{
	int idx;
	snd_rawmidi_t *rmidi = snd_magic_cast(snd_rawmidi_t, device->device_data, return -ENXIO);

	snd_assert(rmidi != NULL, return -ENXIO);
	down(&register_mutex);
	idx = (rmidi->card->number * SNDRV_RAWMIDI_DEVICES) + rmidi->device;
	if (snd_rawmidi_devices[idx] != rmidi) {
		up(&register_mutex);
		return -EINVAL;
	}
	if (rmidi->proc_entry) {
		snd_info_unregister(rmidi->proc_entry);
		rmidi->proc_entry = NULL;
	}
#ifdef CONFIG_SND_OSSEMUL
	if (rmidi->ossreg) {
		if (rmidi->device == snd_midi_map[rmidi->card->number]) {
			snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI, rmidi->card, 0);
			snd_oss_info_unregister(SNDRV_OSS_INFO_DEV_MIDI, rmidi->card->number);
		}
		if (rmidi->device == snd_amidi_map[rmidi->card->number])
			snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI, rmidi->card, 1);
		rmidi->ossreg = 0;
	}
#endif
	if (rmidi->ops && rmidi->ops->dev_unregister)
		rmidi->ops->dev_unregister(rmidi);
	snd_unregister_device(SNDRV_DEVICE_TYPE_RAWMIDI, rmidi->card, rmidi->device);
	snd_rawmidi_devices[idx] = NULL;
	up(&register_mutex);
#ifdef CONFIG_SND_SEQUENCER
	if (rmidi->seq_dev) {
		snd_device_free(rmidi->card, rmidi->seq_dev);
		rmidi->seq_dev = NULL;
	}
#endif
	return snd_rawmidi_free(rmidi);
}

void snd_rawmidi_set_ops(snd_rawmidi_t *rmidi, int stream, snd_rawmidi_ops_t *ops)
{
	struct list_head *list;
	snd_rawmidi_substream_t *substream;
	
	list_for_each(list, &rmidi->streams[stream].substreams) {
		substream = list_entry(list, snd_rawmidi_substream_t, list);
		substream->ops = ops;
	}
}

/*
 *  ENTRY functions
 */

static int __init alsa_rawmidi_init(void)
{
	int i;

	snd_ctl_register_ioctl(snd_rawmidi_control_ioctl);
#ifdef CONFIG_SND_OSSEMUL
	/* check device map table */
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (snd_midi_map[i] < 0 || snd_midi_map[i] >= SNDRV_RAWMIDI_DEVICES) {
			snd_printk("invalid midi_map[%d] = %d\n", i, snd_midi_map[i]);
			snd_midi_map[i] = 0;
		}
		if (snd_amidi_map[i] < 0 || snd_amidi_map[i] >= SNDRV_RAWMIDI_DEVICES) {
			snd_printk("invalid amidi_map[%d] = %d\n", i, snd_amidi_map[i]);
			snd_amidi_map[i] = 1;
		}
	}
#endif /* CONFIG_SND_OSSEMUL */
	return 0;
}

static void __exit alsa_rawmidi_exit(void)
{
	snd_ctl_unregister_ioctl(snd_rawmidi_control_ioctl);
}

module_init(alsa_rawmidi_init)
module_exit(alsa_rawmidi_exit)

EXPORT_SYMBOL(snd_rawmidi_output_params);
EXPORT_SYMBOL(snd_rawmidi_input_params);
EXPORT_SYMBOL(snd_rawmidi_drop_output);
EXPORT_SYMBOL(snd_rawmidi_drain_output);
EXPORT_SYMBOL(snd_rawmidi_drain_input);
EXPORT_SYMBOL(snd_rawmidi_receive_reset);
EXPORT_SYMBOL(snd_rawmidi_receive);
EXPORT_SYMBOL(snd_rawmidi_transmit_reset);
EXPORT_SYMBOL(snd_rawmidi_transmit_empty);
EXPORT_SYMBOL(snd_rawmidi_transmit_peek);
EXPORT_SYMBOL(snd_rawmidi_transmit_ack);
EXPORT_SYMBOL(snd_rawmidi_transmit);
EXPORT_SYMBOL(snd_rawmidi_new);
EXPORT_SYMBOL(snd_rawmidi_set_ops);
EXPORT_SYMBOL(snd_rawmidi_info);
EXPORT_SYMBOL(snd_rawmidi_info_select);
EXPORT_SYMBOL(snd_rawmidi_kernel_open);
EXPORT_SYMBOL(snd_rawmidi_kernel_release);
EXPORT_SYMBOL(snd_rawmidi_kernel_read);
EXPORT_SYMBOL(snd_rawmidi_kernel_write);
