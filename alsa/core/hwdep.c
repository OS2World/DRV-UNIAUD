/*
 *  Hardware dependent layer
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
#include <sound/control.h>
#include <sound/minors.h>
#include <sound/hwdep.h>
#include <sound/info.h>

snd_hwdep_t *snd_hwdep_devices[SNDRV_CARDS * SNDRV_MINOR_HWDEPS];

static DECLARE_MUTEX(register_mutex);

static int snd_hwdep_free(snd_hwdep_t *hwdep);
static int snd_hwdep_dev_free(snd_device_t *device);
static int snd_hwdep_dev_register(snd_device_t *device);
static int snd_hwdep_dev_unregister(snd_device_t *device);

/*

 */

static loff_t snd_hwdep_llseek(struct file * file, loff_t offset, int orig)
{
	snd_hwdep_t *hw = snd_magic_cast(snd_hwdep_t, file->private_data, return -ENXIO);
	if (hw->ops.llseek)
		return hw->ops.llseek(hw, file, offset, orig);
	return -ENXIO;
}

static ssize_t snd_hwdep_read(struct file * file, char *buf, size_t count, loff_t *offset)
{
	snd_hwdep_t *hw = snd_magic_cast(snd_hwdep_t, file->private_data, return -ENXIO);
	if (hw->ops.read)
		return hw->ops.read(hw, buf, count, offset);
	return -ENXIO;	
}

static ssize_t snd_hwdep_write(struct file * file, const char *buf, size_t count, loff_t *offset)
{
	snd_hwdep_t *hw = snd_magic_cast(snd_hwdep_t, file->private_data, return -ENXIO);
	if (hw->ops.write)
		return hw->ops.write(hw, buf, count, offset);
	return -ENXIO;	
}

static int snd_hwdep_open(struct inode *inode, struct file * file)
{
	int major = MAJOR(inode->i_rdev);
	int cardnum;
	int device;
	snd_hwdep_t *hw;
	int err;
	wait_queue_t wait;

	switch (major) {
	case CONFIG_SND_MAJOR:
		cardnum = SNDRV_MINOR_CARD(MINOR(inode->i_rdev));
		device = SNDRV_MINOR_DEVICE(MINOR(inode->i_rdev)) - SNDRV_MINOR_HWDEP;
		break;
#ifdef CONFIG_SND_OSSEMUL
	case SOUND_MAJOR:
		cardnum = SNDRV_MINOR_OSS_CARD(MINOR(inode->i_rdev));
		device = 0;
		break;
#endif
	default:
		return -ENXIO;
	}
	cardnum %= SNDRV_CARDS;
	device %= SNDRV_MINOR_HWDEPS;
	hw = snd_hwdep_devices[(cardnum * SNDRV_MINOR_HWDEPS) + device];

	snd_assert(hw != NULL, return -ENODEV);
	if (!hw->ops.open)
		return -ENXIO;
#ifdef CONFIG_SND_OSSEMUL
	if (major == SOUND_MAJOR && hw->oss_type < 0)
		return -ENXIO;
#endif
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&hw->open_wait, &wait);
	while (1) {
		err = hw->ops.open(hw, file);
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
	remove_wait_queue(&hw->open_wait, &wait);
	if (err >= 0)
		file->private_data = hw;
	return err;
}

static int snd_hwdep_release(struct inode *inode, struct file * file)
{
	int err;
	snd_hwdep_t *hw = snd_magic_cast(snd_hwdep_t, file->private_data, return -ENXIO);
	if (hw->ops.release) {
		err = hw->ops.release(hw, file);
		wake_up(&hw->open_wait);
		return err;
	}
	return -ENXIO;	
}

static unsigned int snd_hwdep_poll(struct file * file, poll_table * wait)
{
	snd_hwdep_t *hw = snd_magic_cast(snd_hwdep_t, file->private_data, return 0);
	if (hw->ops.poll)
		return hw->ops.poll(hw, file, wait);
	return 0;
}

static int snd_hwdep_info(snd_hwdep_t *hw, snd_hwdep_info_t *_info)
{
	snd_hwdep_info_t info;
	
	memset(&info, 0, sizeof(info));
	info.card = hw->card->number;
	strncpy(info.id, hw->id, sizeof(info.id) - 1);	
	strncpy(info.name, hw->name, sizeof(info.name) - 1);
	info.iface = hw->iface;
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int snd_hwdep_ioctl(struct inode *inode, struct file * file,
			   unsigned int cmd, unsigned long arg)
{
	snd_hwdep_t *hw = snd_magic_cast(snd_hwdep_t, file->private_data, return -ENXIO);
	if (cmd == SNDRV_HWDEP_IOCTL_PVERSION)
		return put_user(SNDRV_HWDEP_VERSION, (int *)arg);
	if (cmd == SNDRV_HWDEP_IOCTL_INFO)
		return snd_hwdep_info(hw, (snd_hwdep_info_t *)arg);
	if (hw->ops.ioctl)
		return hw->ops.ioctl(hw, file, cmd, arg);
	return -ENOTTY;
}

#ifndef TARGET_OS2
static int snd_hwdep_mmap(struct file * file, struct vm_area_struct * vma)
{
	snd_hwdep_t *hw = snd_magic_cast(snd_hwdep_t, file->private_data, return -ENXIO);
	if (hw->ops.mmap)
		return hw->ops.mmap(hw, file, vma);
	return -ENXIO;
}
#endif

static int snd_hwdep_control_ioctl(snd_card_t * card, snd_ctl_file_t * control,
				   unsigned int cmd, unsigned long arg)
{
	unsigned int tmp;
	
	tmp = card->number * SNDRV_MINOR_HWDEPS;
	switch (cmd) {
	case SNDRV_CTL_IOCTL_HWDEP_NEXT_DEVICE:
		{
			int device;

			if (get_user(device, (int *)arg))
				return -EFAULT;
			device = device < 0 ? 0 : device + 1;
			while (device < SNDRV_MINOR_HWDEPS) {
				if (snd_hwdep_devices[tmp + device])
					break;
				device++;
			}
			if (device >= SNDRV_MINOR_HWDEPS)
				device = -1;
			if (put_user(device, (int *)arg))
				return -EFAULT;
			return 0;
		}
	case SNDRV_CTL_IOCTL_HWDEP_INFO:
		{
			snd_hwdep_info_t *info = (snd_hwdep_info_t *)arg;
			int device;
			snd_hwdep_t *hwdep;

			if (get_user(device, &info->device))
				return -EFAULT;
			if (device < 0 || device >= SNDRV_MINOR_HWDEPS)
				return -ENXIO;
			hwdep = snd_hwdep_devices[tmp + device];
			if (hwdep == NULL)
				return -ENXIO;
			return snd_hwdep_info(hwdep, info);
		}
	}
	return -ENOIOCTLCMD;
}

/*

 */

#ifdef TARGET_OS2
static struct file_operations snd_hwdep_f_ops =
{
#ifdef LINUX_2_3
	THIS_MODULE,
#endif
	snd_hwdep_llseek,
	snd_hwdep_read,
	snd_hwdep_write,
        0,
	snd_hwdep_poll,
	snd_hwdep_ioctl,
        0,
	snd_hwdep_open,
        0,
	snd_hwdep_release,
	0,0,0,0,0
};

static snd_minor_t snd_hwdep_reg =
{
        {0,0},
        0,0,
	"hardware dependent",
        0,
	&snd_hwdep_f_ops
};
#else
static struct file_operations snd_hwdep_f_ops =
{
#ifdef LINUX_2_3
	owner:		THIS_MODULE,
#endif
	llseek:		snd_hwdep_llseek,
	read:		snd_hwdep_read,
	write:		snd_hwdep_write,
	open:		snd_hwdep_open,
	release:	snd_hwdep_release,
	poll:		snd_hwdep_poll,
	ioctl:		snd_hwdep_ioctl,
	mmap:		snd_hwdep_mmap,
};

static snd_minor_t snd_hwdep_reg =
{
	comment:	"hardware dependent",
	f_ops:		&snd_hwdep_f_ops,
};
#endif

int snd_hwdep_new(snd_card_t * card, char *id, int device, snd_hwdep_t ** rhwdep)
{
	snd_hwdep_t *hwdep;
	int err;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_hwdep_dev_free,
		snd_hwdep_dev_register,
		snd_hwdep_dev_unregister
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_hwdep_dev_free,
		dev_register:	snd_hwdep_dev_register,
		dev_unregister:	snd_hwdep_dev_unregister
	};
#endif

	snd_assert(rhwdep != NULL, return -EINVAL);
	*rhwdep = NULL;
	snd_assert(card != NULL, return -ENXIO);
	hwdep = snd_magic_kcalloc(snd_hwdep_t, 0, GFP_KERNEL);
	if (hwdep == NULL)
		return -ENOMEM;
	hwdep->card = card;
	hwdep->device = device;
	if (id) {
		strncpy(hwdep->id, id, sizeof(hwdep->id) - 1);
	}
#ifdef CONFIG_SND_OSSEMUL
	hwdep->oss_type = -1;
#endif
	if ((err = snd_device_new(card, SNDRV_DEV_HWDEP, hwdep, &ops)) < 0) {
		snd_hwdep_free(hwdep);
		return err;
	}
	init_waitqueue_head(&hwdep->open_wait);
	*rhwdep = hwdep;
	return 0;
}

static int snd_hwdep_free(snd_hwdep_t *hwdep)
{
	snd_assert(hwdep != NULL, return -ENXIO);
	if (hwdep->private_free)
		hwdep->private_free(hwdep);
	snd_magic_kfree(hwdep);
	return 0;
}

static int snd_hwdep_dev_free(snd_device_t *device)
{
	snd_hwdep_t *hwdep = snd_magic_cast(snd_hwdep_t, device->device_data, return -ENXIO);
	return snd_hwdep_free(hwdep);
}

static int snd_hwdep_dev_register(snd_device_t *device)
{
	snd_hwdep_t *hwdep = snd_magic_cast(snd_hwdep_t, device->device_data, return -ENXIO);
	int idx, err;
	char name[32];

	down(&register_mutex);
	idx = (hwdep->card->number * SNDRV_MINOR_HWDEPS) + hwdep->device;
	if (snd_hwdep_devices[idx]) {
		up(&register_mutex);
		return -EBUSY;
	}
	snd_hwdep_devices[idx] = hwdep;
	sprintf(name, "hwC%iD%i", hwdep->card->number, hwdep->device);
	if ((err = snd_register_device(SNDRV_DEVICE_TYPE_HWDEP,
				       hwdep->card, hwdep->device,
				       &snd_hwdep_reg, name)) < 0) {
		snd_printk("unable to register hardware dependant device %i:%i\n",
			   hwdep->card->number, hwdep->device);
		snd_hwdep_devices[idx] = NULL;
		up(&register_mutex);
		return err;
	}
#ifdef CONFIG_SND_OSSEMUL
	hwdep->ossreg = 0;
	if (hwdep->oss_type >= 0) {
		if ((hwdep->oss_type == SNDRV_OSS_DEVICE_TYPE_DMFM) && (hwdep->device != 0)) {
			snd_printk ("only hwdep device 0 can be registered as OSS direct FM device!\n");
		} else {
			if (snd_register_oss_device(hwdep->oss_type,
						    hwdep->card, hwdep->device,
						    &snd_hwdep_reg, hwdep->oss_dev) < 0) {
				snd_printk("unable to register OSS compatibility device %i:%i\n",
					   hwdep->card->number, hwdep->device);
			} else {
				snd_oss_info_register(SNDRV_OSS_INFO_DEV_SYNTH, hwdep->card->number, hwdep->name);
				hwdep->ossreg = 1;
			}
		}
	}
#endif
	up(&register_mutex);
	return 0;
}

static int snd_hwdep_dev_unregister(snd_device_t *device)
{
	snd_hwdep_t *hwdep = snd_magic_cast(snd_hwdep_t, device->device_data, return -ENXIO);
	int idx;

	snd_assert(hwdep != NULL, return -ENXIO);
	down(&register_mutex);
	idx = (hwdep->card->number * SNDRV_MINOR_HWDEPS) + hwdep->device;
	if (snd_hwdep_devices[idx] != hwdep) {
		up(&register_mutex);
		return -EINVAL;
	}
#ifdef CONFIG_SND_OSSEMUL
	if (hwdep->ossreg) {
		snd_unregister_oss_device(hwdep->oss_type, hwdep->card, hwdep->device);
		snd_oss_info_unregister(SNDRV_OSS_INFO_DEV_SYNTH, hwdep->card->number);
	}
#endif
	snd_unregister_device(SNDRV_DEVICE_TYPE_HWDEP, hwdep->card, hwdep->device);
	snd_hwdep_devices[idx] = NULL;
	up(&register_mutex);
	return snd_hwdep_free(hwdep);
}

/*
 *  Info interface
 */

static void snd_hwdep_proc_read(snd_info_entry_t *entry,
				snd_info_buffer_t * buffer)
{
	int idx;
	snd_hwdep_t *hwdep;

	down(&register_mutex);
	for (idx = 0; idx < SNDRV_CARDS * SNDRV_MINOR_HWDEPS; idx++) {
		hwdep = snd_hwdep_devices[idx];
		if (hwdep == NULL)
			continue;
		snd_iprintf(buffer, "%02i-%02i: %s\n",
					idx / SNDRV_MINOR_HWDEPS,
					idx % SNDRV_MINOR_HWDEPS,
					hwdep->name);
	}
	up(&register_mutex);
}

/*
 *  ENTRY functions
 */

static snd_info_entry_t *snd_hwdep_proc_entry = NULL;

static int __init alsa_hwdep_init(void)
{
	snd_info_entry_t *entry;

	memset(snd_hwdep_devices, 0, sizeof(snd_hwdep_devices));
	if ((entry = snd_info_create_module_entry(THIS_MODULE, "hwdep", NULL)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->c.text.read_size = 512;
		entry->c.text.read = snd_hwdep_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_hwdep_proc_entry = entry;
	snd_ctl_register_ioctl(snd_hwdep_control_ioctl);
	return 0;
}

static void __exit alsa_hwdep_exit(void)
{
	snd_ctl_unregister_ioctl(snd_hwdep_control_ioctl);
	if (snd_hwdep_proc_entry) {
		snd_info_unregister(snd_hwdep_proc_entry);
		snd_hwdep_proc_entry = NULL;
	}
}

module_init(alsa_hwdep_init)
module_exit(alsa_hwdep_exit)

EXPORT_SYMBOL(snd_hwdep_new);
