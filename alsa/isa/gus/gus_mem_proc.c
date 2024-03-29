/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  GUS's memory access via proc filesystem
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
#include <sound/gus.h>
#include <sound/info.h>

typedef struct gus_proc_private {
	int rom;		/* data are in ROM */
	unsigned int address;
	unsigned int size;
	snd_gus_card_t * gus;
} gus_proc_private_t;

static long snd_gf1_mem_proc_dump(snd_info_entry_t *entry, void *file_private_data,
			          struct file *file, char *buf, long count)
{
	long size;
	gus_proc_private_t *priv = snd_magic_cast(gus_proc_private_t, entry->private_data, return -ENXIO);
	snd_gus_card_t *gus = priv->gus;
	int err;

	size = count;
	if (file->f_pos + size > priv->size)
		size = (long)priv->size - file->f_pos;
	if (size > 0) {
		if ((err = snd_gus_dram_read(gus, buf, file->f_pos, size, priv->rom)) < 0)
			return err;
		file->f_pos += size;
		return size;
	}
	return 0;
}			

#ifdef TARGET_OS2
static int64_t snd_gf1_mem_proc_llseek(snd_info_entry_t *entry,
					void *private_file_data,
					struct file *file,
					int64_t offset,
					int orig)
#else
static long long snd_gf1_mem_proc_llseek(snd_info_entry_t *entry,
					void *private_file_data,
					struct file *file,
					long long offset,
					int orig)
#endif
{
	gus_proc_private_t *priv = snd_magic_cast(gus_proc_private_t, entry->private_data, return -ENXIO);

	switch (orig) {
	case 0:	/* SEEK_SET */
		file->f_pos = offset;
		break;
	case 1:	/* SEEK_CUR */
		file->f_pos += offset;
		break;
	case 2: /* SEEK_END */
		file->f_pos = priv->size - offset;
		break;
	default:
		return -EINVAL;
	}
	if (file->f_pos > priv->size)
		file->f_pos = priv->size;
	return file->f_pos;
}

static void snd_gf1_mem_proc_free(snd_info_entry_t *entry)
{
	gus_proc_private_t *priv = snd_magic_cast(gus_proc_private_t, entry->private_data, return);
	snd_magic_kfree(priv);
}

#ifdef TARGET_OS2
static struct snd_info_entry_ops snd_gf1_mem_proc_ops = {
        0,0,
	snd_gf1_mem_proc_dump,0,
	snd_gf1_mem_proc_llseek,0,0,0
};
#else
static struct snd_info_entry_ops snd_gf1_mem_proc_ops = {
	read: snd_gf1_mem_proc_dump,
	llseek: snd_gf1_mem_proc_llseek,
};
#endif

int snd_gf1_mem_proc_init(snd_gus_card_t * gus)
{
	int idx;
	char name[16];
	gus_proc_private_t *priv;
	snd_info_entry_t *entry;

	memset(&gus->gf1.rom_entries, 0, sizeof(gus->gf1.rom_entries));
	memset(&gus->gf1.ram_entries, 0, sizeof(gus->gf1.ram_entries));
	for (idx = 0; idx < 4; idx++) {
		if (gus->gf1.mem_alloc.banks_8[idx].size > 0) {
			priv = snd_magic_kcalloc(gus_proc_private_t, 0, GFP_KERNEL);
			if (priv == NULL) {
				snd_gf1_mem_proc_done(gus);
				return -ENOMEM;
			}
			priv->gus = gus;
			sprintf(name, "gus-ram-%i", idx);
			entry = snd_info_create_card_entry(gus->card, name, gus->card->proc_root);
			if (entry) {
				entry->content = SNDRV_INFO_CONTENT_DATA;
				entry->private_data = priv;
				entry->private_free = snd_gf1_mem_proc_free;
				entry->c.ops = &snd_gf1_mem_proc_ops;
				priv->address = gus->gf1.mem_alloc.banks_8[idx].address;
				priv->size = entry->size = gus->gf1.mem_alloc.banks_8[idx].size;
				if (snd_info_register(entry) < 0) {
					snd_info_free_entry(entry);
					entry = NULL;
				}
			}
			gus->gf1.ram_entries[idx] = entry;
		}
	}
	for (idx = 0; idx < 4; idx++) {
		if (gus->gf1.rom_present & (1 << idx)) {
			priv = snd_magic_kcalloc(gus_proc_private_t, 0, GFP_KERNEL);
			if (priv == NULL) {
				snd_gf1_mem_proc_done(gus);
				return -ENOMEM;
			}
			priv->rom = 1;
			priv->gus = gus;
			sprintf(name, "gus-rom-%i", idx);
			entry = snd_info_create_card_entry(gus->card, name, gus->card->proc_root);
			if (entry) {
				entry->content = SNDRV_INFO_CONTENT_DATA;
				entry->private_data = priv;
				entry->private_free = snd_gf1_mem_proc_free;
				entry->c.ops = &snd_gf1_mem_proc_ops;
				priv->address = idx * 4096 * 1024;
				priv->size = entry->size = gus->gf1.rom_memory;
				if (snd_info_register(entry) < 0) {
					snd_info_free_entry(entry);
					entry = NULL;
				}
			}
			gus->gf1.rom_entries[idx] = entry;
		}
	}
	return 0;
}

int snd_gf1_mem_proc_done(snd_gus_card_t * gus)
{
	int idx;

	for (idx = 0; idx < 4; idx++) {
		if (gus->gf1.ram_entries[idx])
			snd_info_unregister(gus->gf1.ram_entries[idx]);
	}
	for (idx = 0; idx < 4; idx++) {
		if (gus->gf1.rom_entries[idx])
			snd_info_unregister(gus->gf1.rom_entries[idx]);
	}
	return 0;
}
