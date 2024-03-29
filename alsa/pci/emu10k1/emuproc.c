/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Creative Labs, Inc.
 *  Routines for control of EMU10K1 chips / proc interface routines
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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

static void snd_emu10k1_proc_spdif_status(emu10k1_t * emu,
					  snd_info_buffer_t * buffer,
					  char *title,
					  int status_reg,
					  int rate_reg)
{
	static char *clkaccy[4] = { "1000ppm", "50ppm", "variable", "unknown" };
	static int samplerate[16] = { 44100, 1, 48000, 32000, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	static char *channel[16] = { "unspec", "left", "right", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15" };
	static char *emphasis[8] = { "none", "50/15 usec 2 channel", "2", "3", "4", "5", "6", "7" };
	unsigned int status, rate = 0;
	
	status = snd_emu10k1_ptr_read(emu, status_reg, 0);
	if (rate_reg > 0)
		rate = snd_emu10k1_ptr_read(emu, rate_reg, 0);

	snd_iprintf(buffer, "\n%s\n", title);

	snd_iprintf(buffer, "Professional Mode     : %s\n", (status & SPCS_PROFESSIONAL) ? "yes" : "no");
	snd_iprintf(buffer, "Not Audio Data        : %s\n", (status & SPCS_NOTAUDIODATA) ? "yes" : "no");
	snd_iprintf(buffer, "Copyright             : %s\n", (status & SPCS_COPYRIGHT) ? "yes" : "no");
	snd_iprintf(buffer, "Emphasis              : %s\n", emphasis[(status & SPCS_EMPHASISMASK) >> 3]);
	snd_iprintf(buffer, "Mode                  : %i\n", (status & SPCS_MODEMASK) >> 6);
	snd_iprintf(buffer, "Category Code         : 0x%x\n", (status & SPCS_CATEGORYCODEMASK) >> 8);
	snd_iprintf(buffer, "Generation Status     : %s\n", status & SPCS_GENERATIONSTATUS ? "original" : "copy");
	snd_iprintf(buffer, "Source Mask           : %i\n", (status & SPCS_SOURCENUMMASK) >> 16);
	snd_iprintf(buffer, "Channel Number        : %s\n", channel[(status & SPCS_CHANNELNUMMASK) >> 20]);
	snd_iprintf(buffer, "Sample Rate           : %iHz\n", samplerate[(status & SPCS_SAMPLERATEMASK) >> 24]);
	snd_iprintf(buffer, "Clock Accuracy        : %s\n", clkaccy[(status & SPCS_CLKACCYMASK) >> 28]);

	if (rate_reg > 0) {
		snd_iprintf(buffer, "S/PDIF Locked         : %s\n", rate & SRCS_SPDIFLOCKED ? "on" : "off");
		snd_iprintf(buffer, "Rate Locked           : %s\n", rate & SRCS_RATELOCKED ? "on" : "off");
		snd_iprintf(buffer, "Estimated Sample Rate : 0x%x\n", rate & SRCS_ESTSAMPLERATE);
	}
}

static void snd_emu10k1_proc_read(snd_info_entry_t *entry, 
				  snd_info_buffer_t * buffer)
{
	static char *outputs[32] = {
		/* 00 */ "PCM Left",
		/* 01 */ "PCM Right",
		/* 02 */ "PCM Surround Left",
		/* 03 */ "PCM Surround Right",
		/* 04 */ "MIDI Left",
		/* 05 */ "MIDI Right",
		/* 06 */ "PCM Center",
		/* 07 */ "PCM LFE",
		/* 08 */ "???",
		/* 09 */ "???",
		/* 10 */ "???",
		/* 11 */ "???",
		/* 12 */ "MIDI Reverb",
		/* 13 */ "MIDI Chorus",
		/* 14 */ "???",
		/* 15 */ "???",
		/* 16 */ "???",
		/* 17 */ "???",
		/* 18 */ "ADC Left / CDROM S/PDIF Left",
		/* 19 */ "ADC Right / CDROM S/PDIF Right",
		/* 20 */ "MIC / Zoom Video Left",
		/* 21 */ "Zoom Video Right",
		/* 22 */ "S/PDIF Left",
		/* 23 */ "S/PDIF Right",
		/* 24 */ "???",
		/* 25 */ "???",
		/* 26 */ "???",
		/* 27 */ "???",
		/* 28 */ "???",
		/* 29 */ "???",
		/* 30 */ "???",
		/* 31 */ "???"
	};
	emu10k1_t *emu = snd_magic_cast(emu10k1_t, entry->private_data, return);
	unsigned int val;
	int idx;
	
	snd_iprintf(buffer, "EMU10K1\n\n");
	val = emu->audigy ?
		snd_emu10k1_ptr_read(emu, A_FXRT1, 0) :
		snd_emu10k1_ptr_read(emu, FXRT, 0);
	snd_iprintf(buffer, "Card                  : %s\n",
		    emu->audigy ? "Audigy" : (emu->APS ? "EMU APS" : "Creative"));
	snd_iprintf(buffer, "Internal TRAM (words) : 0x%x\n", emu->fx8010.itram_size);
	snd_iprintf(buffer, "External TRAM (words) : 0x%x\n", emu->fx8010.etram_size);
	snd_iprintf(buffer, "\n");
	if (emu->audigy) {
		snd_iprintf(buffer, "Effect Send Routing   : A=%i, B=%i, C=%i, D=%i\n",
			    val & 0x3f,
			    (val >> 8) & 0x3f,
			    (val >> 16) & 0x3f,
			    (val >> 24) & 0x3f);
	} else {
		snd_iprintf(buffer, "Effect Send Routing   : A=%i, B=%i, C=%i, D=%i\n",
			    (val >> 16) & 0x0f,
			    (val >> 20) & 0x0f,
			    (val >> 24) & 0x0f,
			    (val >> 28) & 0x0f);
	}
	snd_iprintf(buffer, "\nCaptured FX Outputs   :\n");
	for (idx = 0; idx < 32; idx++) {
		if (emu->efx_voices_mask & (1 << idx))
			snd_iprintf(buffer, "  Output %02i [%s]\n", idx, outputs[idx]);
	}
	snd_iprintf(buffer, "\nAll FX Outputs        :\n");
	for (idx = 0; idx < 32; idx++)
		snd_iprintf(buffer, "  Output %02i [%s]\n", idx, outputs[idx]);
	snd_emu10k1_proc_spdif_status(emu, buffer, "S/PDIF Output 0", SPCS0, -1);
	snd_emu10k1_proc_spdif_status(emu, buffer, "S/PDIF Output 1", SPCS1, -1);
	snd_emu10k1_proc_spdif_status(emu, buffer, "S/PDIF Output 2/3", SPCS2, -1);
	snd_emu10k1_proc_spdif_status(emu, buffer, "CD-ROM S/PDIF", CDCS, CDSRCS);
	snd_emu10k1_proc_spdif_status(emu, buffer, "General purpose S/PDIF", GPSCS, GPSRCS);
	val = snd_emu10k1_ptr_read(emu, ZVSRCS, 0);
	snd_iprintf(buffer, "\nZoomed Video\n");
	snd_iprintf(buffer, "Rate Locked           : %s\n", val & SRCS_RATELOCKED ? "on" : "off");
	snd_iprintf(buffer, "Estimated Sample Rate : 0x%x\n", val & SRCS_ESTSAMPLERATE);
}

static void snd_emu10k1_proc_acode_read(snd_info_entry_t *entry, 
				        snd_info_buffer_t * buffer)
{
	u32 pc;
	emu10k1_t *emu = snd_magic_cast(emu10k1_t, entry->private_data, return);

	snd_iprintf(buffer, "FX8010 Instruction List '%s'\n", emu->fx8010.name);
	snd_iprintf(buffer, "  Code dump      :\n");
	for (pc = 0; pc < 512; pc++) {
		u32 low, high;
			
		low = snd_emu10k1_efx_read(emu, pc * 2);
		high = snd_emu10k1_efx_read(emu, pc * 2 + 1);
		if (emu->audigy)
			snd_iprintf(buffer, "    OP(0x%02x, 0x%03x, 0x%03x, 0x%03x, 0x%03x) /* 0x%04x: 0x%08x%08x */\n",
				    (high >> 24) & 0x0f,
				    (high >> 12) & 0x7ff,
				    (high >> 0) & 0x7ff,
				    (low >> 12) & 0x7ff,
				    (low >> 0) & 0x7ff,
				    pc,
				    high, low);
		else
			snd_iprintf(buffer, "    OP(0x%02x, 0x%03x, 0x%03x, 0x%03x, 0x%03x) /* 0x%04x: 0x%08x%08x */\n",
				    (high >> 20) & 0x0f,
				    (high >> 10) & 0x3ff,
				    (high >> 0) & 0x3ff,
				    (low >> 10) & 0x3ff,
				    (low >> 0) & 0x3ff,
				    pc,
				    high, low);
	}
}

#define TOTAL_SIZE_GPR		(0x100*4)
#define TOTAL_SIZE_TANKMEM_DATA	(0xa0*4)
#define TOTAL_SIZE_TANKMEM_ADDR (0xa0*4)
#define TOTAL_SIZE_CODE		(0x200*8)

static long snd_emu10k1_fx8010_read(snd_info_entry_t *entry, void *file_private_data,
				    struct file *file, char *buf, long count)
{
	long size;
	emu10k1_t *emu = snd_magic_cast(emu10k1_t, entry->private_data, return -ENXIO);
	unsigned int offset;
	
	if (!strcmp(entry->name, "fx8010_tram_addr")) {
		if (emu->audigy) return -EINVAL;
		offset = TANKMEMADDRREGBASE;
	} else if (!strcmp(entry->name, "fx8010_tram_data")) {
		if (emu->audigy) return -EINVAL;
		offset = TANKMEMDATAREGBASE;
	} else if (!strcmp(entry->name, "fx8010_code")) {
		offset = emu->audigy ? A_MICROCODEBASE : MICROCODEBASE;
	} else {
		offset = emu->audigy ? A_FXGPREGBASE : FXGPREGBASE;
	}
	size = count;
	if (file->f_pos + size > entry->size)
		size = (long)entry->size - file->f_pos;
	if (size > 0) {
		unsigned int *tmp;
		long res;
		unsigned int idx;
		if ((tmp = kmalloc(size + 8, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		for (idx = 0; idx < ((file->f_pos & 3) + size + 3) >> 2; idx++)
			tmp[idx] = snd_emu10k1_ptr_read(emu, offset + idx + (file->f_pos >> 2), 0);
		if (copy_to_user(buf, ((char *)tmp) + (file->f_pos & 3), size))
			res = -EFAULT;
		else {
			res = size;
			file->f_pos += size;
		}
		kfree(tmp);
		return res;
	}
	return 0;
}

#ifdef TARGET_OS2
static struct snd_info_entry_ops snd_emu10k1_proc_ops_fx8010 = {
	0, 0, snd_emu10k1_fx8010_read, 0, 0,0,0,0
};
#else
static struct snd_info_entry_ops snd_emu10k1_proc_ops_fx8010 = {
	read: snd_emu10k1_fx8010_read,
};
#endif
int __devinit snd_emu10k1_proc_init(emu10k1_t * emu)
{
	snd_info_entry_t *entry;
	
	if ((entry = snd_info_create_card_entry(emu->card, "emu10k1", emu->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = emu;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 4096;
		entry->c.text.read = snd_emu10k1_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	emu->proc_entry = entry;
	entry = NULL;
	if ((entry = snd_info_create_card_entry(emu->card, "fx8010_gpr", emu->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = emu;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->size = TOTAL_SIZE_GPR;
		entry->c.ops = &snd_emu10k1_proc_ops_fx8010;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	emu->proc_entry_fx8010_gpr = entry;
	entry = NULL;
	if (!emu->audigy && (entry = snd_info_create_card_entry(emu->card, "fx8010_tram_data", emu->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = emu;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->size = TOTAL_SIZE_TANKMEM_DATA;
		entry->c.ops = &snd_emu10k1_proc_ops_fx8010;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	emu->proc_entry_fx8010_tram_data = entry;
	entry = NULL;
	if (!emu->audigy && (entry = snd_info_create_card_entry(emu->card, "fx8010_tram_addr", emu->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = emu;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->size = TOTAL_SIZE_TANKMEM_ADDR;
		entry->c.ops = &snd_emu10k1_proc_ops_fx8010;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	emu->proc_entry_fx8010_tram_addr = entry;
	entry = NULL;
	if ((entry = snd_info_create_card_entry(emu->card, "fx8010_code", emu->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = emu;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->size = TOTAL_SIZE_CODE;
		entry->c.ops = &snd_emu10k1_proc_ops_fx8010;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	emu->proc_entry_fx8010_code = entry;
	entry = NULL;
	if ((entry = snd_info_create_card_entry(emu->card, "fx8010_acode", emu->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = emu;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 64*1024;
		entry->c.text.read = snd_emu10k1_proc_acode_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	emu->proc_entry_fx8010_iblocks = entry;
	return 0;
}

int snd_emu10k1_proc_done(emu10k1_t * emu)
{
	if (emu->proc_entry) {
		snd_info_unregister(emu->proc_entry);
		emu->proc_entry = NULL;
	}
	if (emu->proc_entry_fx8010_gpr) {
		snd_info_unregister(emu->proc_entry_fx8010_gpr);
		emu->proc_entry_fx8010_gpr = NULL;
	}
	if (emu->proc_entry_fx8010_tram_data) {
		snd_info_unregister(emu->proc_entry_fx8010_tram_data);
		emu->proc_entry_fx8010_tram_data = NULL;
	}
	if (emu->proc_entry_fx8010_tram_addr) {
		snd_info_unregister(emu->proc_entry_fx8010_tram_addr);
		emu->proc_entry_fx8010_tram_addr = NULL;
	}
	if (emu->proc_entry_fx8010_code) {
		snd_info_unregister(emu->proc_entry_fx8010_code);
		emu->proc_entry_fx8010_code = NULL;
	}
	if (emu->proc_entry_fx8010_iblocks) {
		snd_info_unregister(emu->proc_entry_fx8010_iblocks);
		emu->proc_entry_fx8010_iblocks = NULL;
	}
	return 0;
}
