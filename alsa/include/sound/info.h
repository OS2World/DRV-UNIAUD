#ifndef __INFO_H
#define __INFO_H

/*
 *  Header file for info interface
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

/* buffer for information */

struct snd_info_buffer {
	char *buffer;		/* pointer to begin of buffer */
	char *curr;		/* current position in buffer */
	unsigned long size;	/* current size */
	unsigned long len;	/* total length of buffer */
	int stop;		/* stop flag */
	int error;		/* error code */
};

typedef struct snd_info_buffer snd_info_buffer_t;

#define SNDRV_INFO_CONTENT_TEXT		0
#define SNDRV_INFO_CONTENT_DATA		1
#define SNDRV_INFO_CONTENT_DEVICE		2

struct snd_info_entry;

struct snd_info_entry_text {
	unsigned long read_size;
	unsigned long write_size;
	void (*read) (snd_info_entry_t *entry, snd_info_buffer_t * buffer);
	void (*write) (snd_info_entry_t *entry, snd_info_buffer_t * buffer);
};

struct snd_info_entry_ops {
	int (*open) (snd_info_entry_t *entry,
		     unsigned short mode, void **file_private_data);
	int (*release) (snd_info_entry_t * entry,
			unsigned short mode, void *file_private_data);
	long (*read) (snd_info_entry_t *entry, void *file_private_data,
		      struct file * file, char *buf, long count);
	long (*write) (snd_info_entry_t *entry, void *file_private_data,
		       struct file * file, const char *buf, long count);
#ifdef TARGET_OS2
	int64_t (*llseek) (snd_info_entry_t *entry, void *file_private_data,
			    struct file * file, int64_t offset, int orig);
#else
	long long (*llseek) (snd_info_entry_t *entry, void *file_private_data,
			    struct file * file, long long offset, int orig);
#endif
	unsigned int (*poll) (snd_info_entry_t *entry, void *file_private_data,
			      struct file * file, poll_table * wait);
	int (*ioctl) (snd_info_entry_t *entry, void *file_private_data,
		      struct file * file, unsigned int cmd, unsigned long arg);
	int (*mmap) (snd_info_entry_t *entry, void *file_private_data,
		     struct inode * inode, struct file * file,
		     struct vm_area_struct * vma);
};

struct snd_info_entry_device {
	unsigned short major;
	unsigned short minor;
};

struct snd_info_entry {
	const char *name;
	mode_t mode;
	long size;
	unsigned short content;
	union {
		struct snd_info_entry_text text;
		struct snd_info_entry_ops *ops;
		struct snd_info_entry_device device;
	} c;
	snd_info_entry_t *parent;
	snd_card_t *card;
	struct module *module;
	void *private_data;
	void (*private_free)(snd_info_entry_t *entry);
	struct proc_dir_entry *p;
	struct semaphore access;
};

extern int snd_info_check_reserved_words(const char *str);

#ifdef CONFIG_SND_OSSEMUL
extern int snd_info_minor_register(void);
extern int snd_info_minor_unregister(void);
#endif


#ifdef CONFIG_PROC_FS

extern snd_info_entry_t *snd_seq_root;

#ifdef TARGET_OS2
int snd_iprintf(snd_info_buffer_t * buffer, char *fmt,...);
#else
int snd_iprintf(snd_info_buffer_t * buffer, char *fmt,...) __attribute__ ((format (printf, 2, 3)));
#endif

int snd_info_init(void);
int snd_info_done(void);

int snd_info_get_line(snd_info_buffer_t * buffer, char *line, int len);
char *snd_info_get_str(char *dest, char *src, int len);
snd_info_entry_t *snd_info_create_module_entry(struct module * module,
					       const char *name,
					       snd_info_entry_t * parent);
snd_info_entry_t *snd_info_create_card_entry(snd_card_t * card,
					     const char *name,
					     snd_info_entry_t * parent);
void snd_info_free_entry(snd_info_entry_t * entry);
snd_info_entry_t *snd_info_create_device(const char *name,
					 unsigned int number,
					 unsigned int mode);
void snd_info_free_device(snd_info_entry_t * entry);
int snd_info_store_text(snd_info_entry_t * entry);
int snd_info_restore_text(snd_info_entry_t * entry);

int snd_info_card_register(snd_card_t * card);
int snd_info_card_unregister(snd_card_t * card);
int snd_info_register(snd_info_entry_t * entry);
int snd_info_unregister(snd_info_entry_t * entry);

struct proc_dir_entry *snd_create_proc_entry(const char *name, mode_t mode,
					     struct proc_dir_entry *parent);
void snd_remove_proc_entry(struct proc_dir_entry *parent,
			   struct proc_dir_entry *de);

#else

#define snd_seq_root NULL

inline int snd_iprintf(snd_info_buffer_t * buffer, char *fmt,...) { return 0; }
inline int snd_info_init(void) { return 0; }
inline int snd_info_done(void) { return 0; }

inline int snd_info_get_line(snd_info_buffer_t * buffer, char *line, int len) { return 0; }
inline char *snd_info_get_str(char *dest, char *src, int len) { return NULL; }
inline snd_info_entry_t *snd_info_create_module_entry(struct module * module, const char *name) { return NULL; }
inline snd_info_entry_t *snd_info_create_card_entry(snd_card_t * card, const char *name) { return NULL; }
inline void snd_info_free_entry(snd_info_entry_t * entry) { ; }
inline snd_info_entry_t *snd_info_create_device(const char *name,
						       unsigned int number,
						       unsigned int mode) { return NULL; }
inline void snd_info_free_device(snd_info_entry_t * entry) { ; }

inline int snd_info_card_register(snd_card_t * card) { return 0; }
inline int snd_info_card_unregister(snd_card_t * card) { return 0; }
inline int snd_info_register(snd_info_entry_t * entry) { return 0; }
inline int snd_info_unregister(snd_info_entry_t * entry) { return 0; }

inline struct proc_dir_entry *snd_create_proc_entry(const char *name, mode_t mode, struct proc_dir_entry *parent) { return 0; }
inline void snd_remove_proc_entry(struct proc_dir_entry *parent,
					 struct proc_dir_entry *de) { ; }

#endif

/*
 * OSS info part
 */

#ifdef CONFIG_SND_OSSEMUL

#define SNDRV_OSS_INFO_DEV_AUDIO	0
#define SNDRV_OSS_INFO_DEV_SYNTH	1
#define SNDRV_OSS_INFO_DEV_MIDI	2
#define SNDRV_OSS_INFO_DEV_TIMERS	4
#define SNDRV_OSS_INFO_DEV_MIXERS	5

#define SNDRV_OSS_INFO_DEV_COUNT	6

extern int snd_oss_info_register(int dev, int num, char *string);
#define snd_oss_info_unregister(dev, num) snd_oss_info_register(dev, num, NULL)

#endif				/* CONFIG_SND_OSSEMUL */

#endif				/* __INFO_H */
