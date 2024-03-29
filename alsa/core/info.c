/*
 *  Information interface for ALSA driver
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

#include <sound/driver.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/version.h>
#include <stdarg.h>
#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif

/*
 *
 */

int snd_info_check_reserved_words(const char *str)
{
	static char *reserved[] =
	{
		"dev",
		"version",
		"meminfo",
		"memdebug",
		"detect",
		"devices",
		"oss-devices",
		"cards",
		"timers",
		"synth",
		"pcm",
		"seq",
		NULL
	};
	char **xstr = reserved;

	while (*xstr) {
		if (!strcmp(*xstr, str))
			return 0;
		xstr++;
	}
	if (!strncmp(str, "card", 4))
		return 0;
	return 1;
}

#ifdef CONFIG_PROC_FS

extern int snd_major;
extern struct file_operations snd_fops;

static DECLARE_MUTEX(info_mutex);

typedef struct _snd_info_private_data {
	snd_info_buffer_t *rbuffer;
	snd_info_buffer_t *wbuffer;
	snd_info_entry_t *entry;
	void *file_private_data;
} snd_info_private_data_t;

static int snd_info_version_init(void);
static int snd_info_version_done(void);

/*

 */

#ifndef TARGET_OS2
int snd_iprintf(snd_info_buffer_t * buffer, char *fmt,...)
{
	va_list args;
	int res;
	char sbuffer[512];

	if (buffer->stop || buffer->error)
		return 0;
	va_start(args, fmt);
	res = vsprintf(sbuffer, fmt, args);
	va_end(args);
	if (buffer->size + res >= buffer->len) {
		buffer->stop = 1;
		return 0;
	}
	strcpy(buffer->curr, sbuffer);
	buffer->curr += res;
	buffer->size += res;
	return res;
}
#endif

/*

 */

struct proc_dir_entry *snd_proc_root = NULL;
struct proc_dir_entry *snd_proc_dev = NULL;
snd_info_entry_t *snd_seq_root = NULL;

#ifndef LINUX_2_3
static void snd_info_fill_inode(struct inode *inode, int fill)
{
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
}

static inline void snd_info_entry_prepare(struct proc_dir_entry *de)
{
	de->fill_inode = snd_info_fill_inode;
}

void snd_remove_proc_entry(struct proc_dir_entry *parent,
			   struct proc_dir_entry *de)
{
	if (parent && de)
		proc_unregister(parent, de->low_ino);
}
#else
static inline void snd_info_entry_prepare(struct proc_dir_entry *de)
{
	de->owner = THIS_MODULE;
}

void snd_remove_proc_entry(struct proc_dir_entry *parent,
			   struct proc_dir_entry *de)
{
	if (de)
		remove_proc_entry(de->name, parent);
}
#endif

static loff_t snd_info_entry_llseek(struct file *file, loff_t offset, int orig)
{
	snd_info_private_data_t *data;
	struct snd_info_entry *entry;

	data = snd_magic_cast(snd_info_private_data_t, file->private_data, return -ENXIO);
	entry = data->entry;
	switch (entry->content) {
	case SNDRV_INFO_CONTENT_TEXT:
		switch (orig) {
		case 0:	/* SEEK_SET */
			file->f_pos = offset;
			return file->f_pos;
		case 1:	/* SEEK_CUR */
			file->f_pos += offset;
			return file->f_pos;
		case 2:	/* SEEK_END */
		default:
			return -EINVAL;
		}
		break;
	case SNDRV_INFO_CONTENT_DATA:
		if (entry->c.ops->llseek)
			return entry->c.ops->llseek(entry,
						    data->file_private_data,
						    file, offset, orig);
		break;
	}
	return -ENXIO;
}

static ssize_t snd_info_entry_read(struct file *file, char *buffer,
				   size_t count, loff_t * offset)
{
	snd_info_private_data_t *data;
	struct snd_info_entry *entry;
	snd_info_buffer_t *buf;
	long size = 0, size1;

	data = snd_magic_cast(snd_info_private_data_t, file->private_data, return -ENXIO);
	snd_assert(data != NULL, return -ENXIO);
	entry = data->entry;
	switch (entry->content) {
	case SNDRV_INFO_CONTENT_TEXT:
		buf = data->rbuffer;
		if (buf == NULL)
			return -EIO;
		if (file->f_pos >= buf->size)
			return 0;
		size = buf->size < count ? buf->size : count;
		size1 = buf->size - file->f_pos;
		if (size1 < size)
			size = size1;
		if (copy_to_user(buffer, buf->buffer + file->f_pos, size))
			return -EFAULT;
		file->f_pos += size;
		break;
	case SNDRV_INFO_CONTENT_DATA:
		if (entry->c.ops->read)
			return entry->c.ops->read(entry,
						  data->file_private_data,
						  file, buffer, count);
		if (size > 0)
			file->f_pos += size;
		break;
	}
	return size;
}

static ssize_t snd_info_entry_write(struct file *file, const char *buffer,
				    size_t count, loff_t * offset)
{
	snd_info_private_data_t *data;
	struct snd_info_entry *entry;
	snd_info_buffer_t *buf;
	long size = 0, size1;

	data = snd_magic_cast(snd_info_private_data_t, file->private_data, return -ENXIO);
	snd_assert(data != NULL, return -ENXIO);
	entry = data->entry;
	switch (entry->content) {
	case SNDRV_INFO_CONTENT_TEXT:
		buf = data->wbuffer;
		if (buf == NULL)
			return -EIO;
		if (file->f_pos < 0)
			return -EINVAL;
		if (file->f_pos >= buf->len)
			return -ENOMEM;
		size = buf->len < count ? buf->len : count;
		size1 = buf->len - file->f_pos;
		if (size1 < size)
			size = size1;
		if (copy_from_user(buf->buffer + file->f_pos, buffer, size))
			return -EFAULT;
		if (buf->size < file->f_pos + size)
			buf->size = file->f_pos + size;
		file->f_pos += size;
		break;
	case SNDRV_INFO_CONTENT_DATA:
		if (entry->c.ops->write)
			return entry->c.ops->write(entry,
						   data->file_private_data,
						   file, buffer, count);
		if (size > 0)
			file->f_pos += size;
		break;
	}
	return size;
}

#ifndef TARGET_OS2
static int snd_info_entry_open(struct inode *inode, struct file *file)
{
	snd_info_entry_t *entry;
	snd_info_private_data_t *data;
	snd_info_buffer_t *buffer;
	struct proc_dir_entry *p;
	int mode, err;

	down(&info_mutex);
	p = (struct proc_dir_entry *) inode->u.generic_ip;
	entry = p == NULL ? NULL : (snd_info_entry_t *)p->data;
	if (entry == NULL) {
		up(&info_mutex);
		return -ENODEV;
	}
#ifndef LINUX_2_3
	MOD_INC_USE_COUNT;
#endif
	if (entry->module && !try_inc_mod_count(entry->module)) {
		err = -EFAULT;
		goto __error1;
	}
	mode = file->f_flags & O_ACCMODE;
	if (mode == O_RDONLY || mode == O_RDWR) {
		if ((entry->content == SNDRV_INFO_CONTENT_TEXT &&
		     !entry->c.text.read_size) ||
		    (entry->content == SNDRV_INFO_CONTENT_DATA &&
		     entry->c.ops->read == NULL) ||
		    entry->content == SNDRV_INFO_CONTENT_DEVICE) {
		    	err = -ENODEV;
		    	goto __error;
		}
	}
	if (mode == O_WRONLY || mode == O_RDWR) {
		if ((entry->content == SNDRV_INFO_CONTENT_TEXT &&
					!entry->c.text.write_size) ||
		    (entry->content == SNDRV_INFO_CONTENT_DATA &&
		    			entry->c.ops->write == NULL) ||
		    entry->content == SNDRV_INFO_CONTENT_DEVICE) {
		    	err = -ENODEV;
		    	goto __error;
		}
	}
	data = snd_magic_kcalloc(snd_info_private_data_t, 0, GFP_KERNEL);
	if (data == NULL) {
		err = -ENOMEM;
		goto __error;
	}
	data->entry = entry;
	switch (entry->content) {
	case SNDRV_INFO_CONTENT_TEXT:
		if (mode == O_RDONLY || mode == O_RDWR) {
			buffer = (snd_info_buffer_t *)
				 	snd_kcalloc(sizeof(snd_info_buffer_t), GFP_KERNEL);
			if (buffer == NULL) {
				snd_magic_kfree(data);
				err = -ENOMEM;
				goto __error;
			}
			buffer->len = (entry->c.text.read_size +
				      (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
			buffer->buffer = vmalloc(buffer->len);
			if (buffer->buffer == NULL) {
				kfree(buffer);
				snd_magic_kfree(data);
				err = -ENOMEM;
				goto __error;
			}
			buffer->curr = buffer->buffer;
			data->rbuffer = buffer;
		}
		if (mode == O_WRONLY || mode == O_RDWR) {
			buffer = (snd_info_buffer_t *)
					snd_kcalloc(sizeof(snd_info_buffer_t), GFP_KERNEL);
			if (buffer == NULL) {
				if (mode == O_RDWR) {
					vfree(data->rbuffer->buffer);
					kfree(data->rbuffer);
				}
				snd_magic_kfree(data);
				err = -ENOMEM;
				goto __error;
			}
			buffer->len = (entry->c.text.write_size +
				      (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
			buffer->buffer = vmalloc(buffer->len);
			if (buffer->buffer == NULL) {
				if (mode == O_RDWR) {
					vfree(data->rbuffer->buffer);
					kfree(data->rbuffer);
				}
				kfree(buffer);
				snd_magic_kfree(data);
				err = -ENOMEM;
				goto __error;
			}
			buffer->curr = buffer->buffer;
			data->wbuffer = buffer;
		}
		break;
	case SNDRV_INFO_CONTENT_DATA:	/* data */
		if (entry->c.ops->open) {
			if ((err = entry->c.ops->open(entry, mode,
						      &data->file_private_data)) < 0) {
				snd_magic_kfree(data);
				goto __error;
			}
		}
		break;
	}
	file->private_data = data;
	up(&info_mutex);
	if (entry->content == SNDRV_INFO_CONTENT_TEXT &&
	    (mode == O_RDONLY || mode == O_RDWR)) {
		if (entry->c.text.read) {
			down(&entry->access);
			entry->c.text.read(entry, data->rbuffer);
			up(&entry->access);
		}
	}
	return 0;

      __error:
	dec_mod_count(entry->module);
      __error1:
#ifndef LINUX_2_3
	MOD_DEC_USE_COUNT;
#endif
	up(&info_mutex);
	return err;
}
#endif

static int snd_info_entry_release(struct inode *inode, struct file *file)
{
	snd_info_entry_t *entry;
	snd_info_private_data_t *data;
	int mode;

	mode = file->f_flags & O_ACCMODE;
	data = snd_magic_cast(snd_info_private_data_t, file->private_data, return -ENXIO);
	entry = data->entry;
	switch (entry->content) {
	case SNDRV_INFO_CONTENT_TEXT:
		if (mode == O_RDONLY || mode == O_RDWR) {
			vfree(data->rbuffer->buffer);
			kfree(data->rbuffer);
		}
		if (mode == O_WRONLY || mode == O_RDWR) {
			if (entry->c.text.write) {
				entry->c.text.write(entry, data->wbuffer);
				if (data->wbuffer->error) {
					snd_printk("data write error to %s (%i)\n",
						entry->name,
						data->wbuffer->error);
				}
			}
			vfree(data->wbuffer->buffer);
			kfree(data->wbuffer);
		}
		break;
	case SNDRV_INFO_CONTENT_DATA:
		if (entry->c.ops->release)
			entry->c.ops->release(entry, mode,
					      data->file_private_data);
		break;
	}
	dec_mod_count(entry->module);
#ifndef LINUX_2_3
	MOD_DEC_USE_COUNT;
#endif
	snd_magic_kfree(data);
	return 0;
}

static unsigned int snd_info_entry_poll(struct file *file, poll_table * wait)
{
	snd_info_private_data_t *data;
	struct snd_info_entry *entry;
	unsigned int mask;

	data = snd_magic_cast(snd_info_private_data_t, file->private_data, return -ENXIO);
	if (data == NULL)
		return 0;
	entry = data->entry;
	mask = 0;
	switch (entry->content) {
	case SNDRV_INFO_CONTENT_DATA:
		if (entry->c.ops->poll)
			return entry->c.ops->poll(entry,
						  data->file_private_data,
						  file, wait);
		if (entry->c.ops->read)
			mask |= POLLIN | POLLRDNORM;
		if (entry->c.ops->write)
			mask |= POLLOUT | POLLWRNORM;
		break;
	}
	return mask;
}

static int snd_info_entry_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	snd_info_private_data_t *data;
	struct snd_info_entry *entry;

	data = snd_magic_cast(snd_info_private_data_t, file->private_data, return -ENXIO);
	if (data == NULL)
		return 0;
	entry = data->entry;
	switch (entry->content) {
	case SNDRV_INFO_CONTENT_DATA:
		if (entry->c.ops->ioctl)
			return entry->c.ops->ioctl(entry,
						   data->file_private_data,
						   file, cmd, arg);
		break;
	}
	return -ENOTTY;
}

static int snd_info_entry_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file->f_dentry->d_inode;
	snd_info_private_data_t *data;
	struct snd_info_entry *entry;

	data = snd_magic_cast(snd_info_private_data_t, file->private_data, return -ENXIO);
	if (data == NULL)
		return 0;
	entry = data->entry;
	switch (entry->content) {
	case SNDRV_INFO_CONTENT_DATA:
		if (entry->c.ops->mmap)
			return entry->c.ops->mmap(entry,
						  data->file_private_data,
						  inode, file, vma);
		break;
	}
	return -ENXIO;
}

#ifdef TARGET_OS2
static struct file_operations snd_info_entry_operations =
{
#ifdef LINUX_2_3
	THIS_MODULE,
#endif
	snd_info_entry_llseek,
	snd_info_entry_read,
	snd_info_entry_write,
        0,
	snd_info_entry_poll,
	snd_info_entry_ioctl,
	snd_info_entry_mmap,
        0,
//	snd_info_entry_open,
        0,
	snd_info_entry_release,
        0,0,0,0,0
};
#else
static struct file_operations snd_info_entry_operations =
{
#ifdef LINUX_2_3
	owner:		THIS_MODULE,
#endif
	llseek:		snd_info_entry_llseek,
	read:		snd_info_entry_read,
	write:		snd_info_entry_write,
	poll:		snd_info_entry_poll,
	ioctl:		snd_info_entry_ioctl,
	mmap:		snd_info_entry_mmap,
	open:		snd_info_entry_open,
	release:	snd_info_entry_release,
};

#ifndef LINUX_2_3
static struct inode_operations snd_info_entry_inode_operations =
{
	&snd_info_entry_operations,	/* default sound info directory file-ops */
};

static struct inode_operations snd_info_device_inode_operations =
{
	&snd_fops,		/* default sound info directory file-ops */
};
#endif	/* LINUX_2_3 */

static int snd_info_card_readlink(struct dentry *dentry,
				  char *buffer, int buflen)
{
        char *s = ((struct proc_dir_entry *) dentry->d_inode->u.generic_ip)->data;
#ifdef LINUX_2_3
	return vfs_readlink(dentry, buffer, buflen, s);
#else
	int len;
	
	if (s == NULL)
		return -EIO;
	len = strlen(s);
	if (len > buflen)
		len = buflen;
	if (copy_to_user(buffer, s, len))
		return -EFAULT;
	return len;
#endif
}

#ifdef LINUX_2_3
static int snd_info_card_followlink(struct dentry *dentry,
				    struct nameidata *nd)
{
        char *s = ((struct proc_dir_entry *) dentry->d_inode->u.generic_ip)->data;
        return vfs_follow_link(nd, s);
}
#else
static struct dentry *snd_info_card_followlink(struct dentry *dentry,
					       struct dentry *base,
					       unsigned int follow)
{
	char *s = ((struct proc_dir_entry *) dentry->d_inode->u.generic_ip)->data;
	return lookup_dentry(s, base, follow);
}
#endif

#ifndef LINUX_2_3
static struct file_operations snd_info_card_link_operations =
{
	NULL
};
#endif

struct inode_operations snd_info_card_link_inode_operations =
{
#ifndef LINUX_2_3
	default_file_ops:	&snd_info_card_link_operations,
#endif
	readlink:		snd_info_card_readlink,
	follow_link:		snd_info_card_followlink,
};
#endif //TARGET_OS2

struct proc_dir_entry *snd_create_proc_entry(const char *name, mode_t mode,
					     struct proc_dir_entry *parent)
{
	struct proc_dir_entry *p;
	p = create_proc_entry(name, mode, parent);
	if (p)
		snd_info_entry_prepare(p);
	return p;
}

int __init snd_info_init(void)
{
	struct proc_dir_entry *p;

	p = snd_create_proc_entry("asound", S_IFDIR | S_IRUGO | S_IXUGO, &proc_root);
	if (p == NULL)
		return -ENOMEM;
	snd_proc_root = p;
	p = snd_create_proc_entry("dev", S_IFDIR | S_IRUGO | S_IXUGO, snd_proc_root);
	if (p == NULL)
		return -ENOMEM;
	snd_proc_dev = p;
#ifdef CONFIG_SND_SEQUENCER
	{
		snd_info_entry_t *entry;
		if ((entry = snd_info_create_module_entry(THIS_MODULE, "seq", NULL)) == NULL)
			return -ENOMEM;
		entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			return -ENOMEM;
		}
		snd_seq_root = entry;
	}
#endif
	snd_info_version_init();
#ifdef CONFIG_SND_DEBUG_MEMORY
	snd_memory_info_init();
#endif
	snd_minor_info_init();
#ifdef CONFIG_SND_OSSEMUL
	snd_minor_info_oss_init();
#endif
	snd_card_info_init();
	return 0;
}

int __exit snd_info_done(void)
{
	snd_card_info_done();
#ifdef CONFIG_SND_OSSEMUL
	snd_minor_info_oss_done();
#endif
	snd_minor_info_done();
#ifdef CONFIG_SND_DEBUG_MEMORY
	snd_memory_info_done();
#endif
	snd_info_version_done();
	if (snd_proc_root) {
#ifdef CONFIG_SND_SEQUENCER
		if (snd_seq_root)
			snd_info_unregister(snd_seq_root);
#endif
		snd_remove_proc_entry(snd_proc_root, snd_proc_dev);
		snd_remove_proc_entry(&proc_root, snd_proc_root);
	}
	return 0;
}

/*

 */


int snd_info_card_register(snd_card_t * card)
{
	char str[8];
	char *s;
	snd_info_entry_t *entry;
	struct proc_dir_entry *p;

	snd_assert(card != NULL, return -ENXIO);

	sprintf(str, "card%i", card->number);
	if ((entry = snd_info_create_module_entry(card->module, str, NULL)) == NULL)
		return -ENOMEM;
	entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	card->proc_root = entry;

	if (!strcmp(card->id, str))
		return 0;

	s = snd_kmalloc_strdup(str, GFP_KERNEL);
	if (s == NULL)
		return -ENOMEM;
	p = snd_create_proc_entry(card->id, S_IFLNK | S_IRUGO | S_IWUGO | S_IXUGO, snd_proc_root);
	if (p == NULL)
		return -ENOMEM;
	p->data = s;
#ifndef TARGET_OS2
#ifdef LINUX_2_3
	p->owner = card->module;
	p->proc_iops = &snd_info_card_link_inode_operations;
#else
	p->ops = &snd_info_card_link_inode_operations;
#endif
#endif
	card->proc_root_link = p;
	return 0;
}

int snd_info_card_unregister(snd_card_t * card)
{
	void *data;

	snd_assert(card != NULL, return -ENXIO);
	if (card->proc_root_link) {
		data = card->proc_root_link->data;
		card->proc_root_link->data = NULL;
		kfree(data);
		snd_remove_proc_entry(snd_proc_root, card->proc_root_link);
		card->proc_root_link = NULL;
	}
	if (card->proc_root) {
		snd_info_unregister(card->proc_root);
		card->proc_root = NULL;
	}
	return 0;
}

/*

 */

int snd_info_get_line(snd_info_buffer_t * buffer, char *line, int len)
{
	int c = -1;

	if (len <= 0 || buffer->stop || buffer->error)
		return 1;
	while (--len > 0) {
		c = *buffer->curr++;
		if (c == '\n') {
			if ((buffer->curr - buffer->buffer) >= buffer->size) {
				buffer->stop = 1;
			}
			break;
		}
		*line++ = c;
		if ((buffer->curr - buffer->buffer) >= buffer->size) {
			buffer->stop = 1;
			break;
		}
	}
	while (c != '\n' && !buffer->stop) {
		c = *buffer->curr++;
		if ((buffer->curr - buffer->buffer) >= buffer->size) {
			buffer->stop = 1;
		}
	}
	*line = '\0';
	return 0;
}

char *snd_info_get_str(char *dest, char *src, int len)
{
	int c;

	while (*src == ' ' || *src == '\t')
		src++;
	if (*src == '"' || *src == '\'') {
		c = *src++;
		while (--len > 0 && *src && *src != c) {
			*dest++ = *src++;
		}
		if (*src == c)
			src++;
	} else {
		while (--len > 0 && *src && *src != ' ' && *src != '\t') {
			*dest++ = *src++;
		}
	}
	*dest = 0;
	while (*src == ' ' || *src == '\t')
		src++;
	return src;
}

static snd_info_entry_t *snd_info_create_entry(const char *name)
{
	snd_info_entry_t *entry;
	entry = (snd_info_entry_t *) snd_kcalloc(sizeof(snd_info_entry_t), GFP_KERNEL);
	if (entry == NULL)
		return NULL;
	entry->name = snd_kmalloc_strdup(name, GFP_KERNEL);
	if (entry->name == NULL) {
		kfree(entry);
		return NULL;
	}
	entry->mode = S_IFREG | S_IRUGO;
	entry->content = SNDRV_INFO_CONTENT_TEXT;
	init_MUTEX(&entry->access);
	return entry;
}

snd_info_entry_t *snd_info_create_module_entry(struct module * module,
					       const char *name,
					       snd_info_entry_t *parent)
{
	snd_info_entry_t *entry = snd_info_create_entry(name);
	if (entry) {
		entry->module = module;
		entry->parent = parent;
	}
	return entry;
}

snd_info_entry_t *snd_info_create_card_entry(snd_card_t * card,
					     const char *name,
					     snd_info_entry_t * parent)
{
	snd_info_entry_t *entry = snd_info_create_entry(name);
	if (entry) {
		entry->module = card->module;
		entry->card = card;
		entry->parent = parent;
	}
	return entry;
}

void snd_info_free_entry(snd_info_entry_t * entry)
{
	if (entry == NULL)
		return;
	if (entry->name)
		kfree((char *)entry->name);
	if (entry->private_free)
		entry->private_free(entry);
	kfree(entry);
}

#ifndef TARGET_OS2
#ifndef LINUX_2_3
static void snd_info_device_fill_inode(struct inode *inode, int fill)
{
	struct proc_dir_entry *de;
	snd_info_entry_t *entry;

	if (!fill) {
		MOD_DEC_USE_COUNT;
		return;
	}
	MOD_INC_USE_COUNT;
	de = (struct proc_dir_entry *) inode->u.generic_ip;
	if (de == NULL)
		return;
	entry = (snd_info_entry_t *) de->data;
	if (entry == NULL)
		return;
	inode->i_rdev = MKDEV(entry->c.device.major, entry->c.device.minor);
}

static inline void snd_info_device_entry_prepare(struct proc_dir_entry *de, snd_info_entry_t *entry)
{
	de->fill_inode = snd_info_device_fill_inode;
}
#else
static inline void snd_info_device_entry_prepare(struct proc_dir_entry *de, snd_info_entry_t *entry)
{
	de->rdev = MKDEV(entry->c.device.major, entry->c.device.minor);
	de->owner = THIS_MODULE;
}
#endif /* LINUX_2_3 */
#endif

snd_info_entry_t *snd_info_create_device(const char *name, unsigned int number, unsigned int mode)
{
#ifdef CONFIG_DEVFS_FS
	char dname[32];
#endif
	unsigned short major = number >> 16;
	unsigned short minor = (unsigned short) number;
	snd_info_entry_t *entry;
	struct proc_dir_entry *p = NULL;

	if (!major)
		major = snd_major;
	if (!mode)
		mode = S_IFCHR | S_IRUGO | S_IWUGO;
	mode &= (snd_device_mode & (S_IRUGO | S_IWUGO)) | S_IFCHR | S_IFBLK;
	entry = snd_info_create_module_entry(THIS_MODULE, name, NULL);
	if (entry == NULL)
		return NULL;
	entry->content = SNDRV_INFO_CONTENT_DEVICE;
	entry->mode = mode;
	entry->c.device.major = major;
	entry->c.device.minor = minor;
	down(&info_mutex);
	p = create_proc_entry(entry->name, entry->mode, snd_proc_dev);
	if (p) {
#ifndef TARGET_OS2
		snd_info_device_entry_prepare(p, entry);
#ifdef LINUX_2_3
		p->proc_fops = &snd_fops;
#else
		p->ops = &snd_info_device_inode_operations;
#endif
#endif
	} else {
		up(&info_mutex);
		snd_info_free_entry(entry);
		return NULL;
	}
	p->gid = snd_device_gid;
	p->uid = snd_device_uid;
	p->data = (void *) entry;
	entry->p = p;
	up(&info_mutex);
#ifdef CONFIG_DEVFS_FS
	if (strncmp(name, "controlC", 8)) {	/* created in sound.c */
		sprintf(dname, "snd/%s", name);
		devfs_register(NULL, dname, DEVFS_FL_DEFAULT,
				major, minor, mode,
				&snd_fops, NULL);
	}
#endif
	return entry;
}

void snd_info_free_device(snd_info_entry_t * entry)
{
#ifdef CONFIG_DEVFS_FS
	char dname[32];
	devfs_handle_t master;
#endif

	snd_runtime_check(entry, return);
	down(&info_mutex);
	snd_remove_proc_entry(snd_proc_dev, entry->p);
	up(&info_mutex);
#ifdef CONFIG_DEVFS_FS
	if (entry->p && strncmp(entry->name, "controlC", 8)) {
		sprintf(dname, "snd/%s", entry->name);
		master = devfs_find_handle(NULL, dname, 0, 0, DEVFS_SPECIAL_CHR, 0);
		devfs_unregister(master);
	}
#endif
	snd_info_free_entry(entry);
}

int snd_info_register(snd_info_entry_t * entry)
{
	struct proc_dir_entry *root, *p = NULL;

	snd_assert(entry != NULL, return -ENXIO);
	root = entry->parent == NULL ? snd_proc_root : entry->parent->p;
	down(&info_mutex);
	p = snd_create_proc_entry(entry->name, entry->mode, root);
	if (!p) {
		up(&info_mutex);
		return -ENOMEM;
	}
#ifdef LINUX_2_3
	p->owner = entry->module;
#endif

#ifndef TARGET_OS2
	if (!S_ISDIR(entry->mode)) {
#ifdef LINUX_2_3
		p->proc_fops = &snd_info_entry_operations;
#else
		p->ops = &snd_info_entry_inode_operations;
#endif
	}
#endif
	p->size = entry->size;
	p->data = entry;
	entry->p = p;
	up(&info_mutex);
	return 0;
}

int snd_info_unregister(snd_info_entry_t * entry)
{
	struct proc_dir_entry *root;

	snd_assert(entry != NULL && entry->p != NULL, return -ENXIO);
	root = entry->parent == NULL ? snd_proc_root : entry->parent->p;
	snd_assert(root, return -ENXIO);
	down(&info_mutex);
	snd_remove_proc_entry(root, entry->p);
	up(&info_mutex);
	snd_info_free_entry(entry);
	return 0;
}

/*

 */

static snd_info_entry_t *snd_info_version_entry = NULL;

static void snd_info_version_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	static char *kernel_version = UTS_RELEASE;

	snd_iprintf(buffer,
		    "Advanced Linux Sound Architecture Driver Version " CONFIG_SND_VERSION ".\n"
		    "Compiled on " __DATE__ " for kernel %s"
#ifdef __SMP__
		    " (SMP)"
#endif
#ifdef MODVERSIONS
		    " with versioned symbols"
#endif
		    ".\n", kernel_version);
}

static int __init snd_info_version_init(void)
{
	snd_info_entry_t *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "version", NULL);
	if (entry == NULL)
		return -ENOMEM;
	entry->c.text.read_size = 256;
	entry->c.text.read = snd_info_version_read;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	snd_info_version_entry = entry;
	return 0;
}

static int __exit snd_info_version_done(void)
{
	if (snd_info_version_entry)
		snd_info_unregister(snd_info_version_entry);
	return 0;
}

#endif /* CONFIG_PROC_FS */
