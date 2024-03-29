/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for Sound Blaster mixer control
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
#include <sound/sb.h>
#include <sound/control.h>

#define chip_t sb_t

#undef IO_DEBUG

void snd_sbmixer_write(sb_t *chip, unsigned char reg, unsigned char data)
{
	outb(reg, SBP(chip, MIXER_ADDR));
	udelay(10);
	outb(data, SBP(chip, MIXER_DATA));
	udelay(10);
#ifdef IO_DEBUG
	snd_printk("mixer_write 0x%x 0x%x\n", reg, data);
#endif
}

unsigned char snd_sbmixer_read(sb_t *chip, unsigned char reg)
{
	unsigned char result;

	outb(reg, SBP(chip, MIXER_ADDR));
	udelay(10);
	result = inb(SBP(chip, MIXER_DATA));
	udelay(10);
#ifdef IO_DEBUG
	snd_printk("mixer_read 0x%x 0x%x\n", reg, result);
#endif
	return result;
}

/*
 * Single channel mixer element
 */

#ifdef TARGET_OS2
#define SB_SINGLE(xname, reg, shift, mask) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0,0,\
  xname, 0,0,\
  snd_sbmixer_info_single, \
  snd_sbmixer_get_single, snd_sbmixer_put_single, \
  reg | (shift << 16) | (mask << 24) }
#else
#define SB_SINGLE(xname, reg, shift, mask) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, \
  name: xname, \
  info: snd_sbmixer_info_single, \
  get: snd_sbmixer_get_single, put: snd_sbmixer_put_single, \
  private_value: reg | (shift << 16) | (mask << 24) }
#endif

static int snd_sbmixer_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_sbmixer_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	sb_t *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 16) & 0xff;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	unsigned char val;

	spin_lock_irqsave(&sb->mixer_lock, flags);
	val = (snd_sbmixer_read(sb, reg) >> shift) & mask;
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

static int snd_sbmixer_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	sb_t *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 16) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int change;
	unsigned char val, oval;

	val = (ucontrol->value.integer.value[0] & mask) << shift;
	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, reg);
	val = (oval & ~(mask << shift)) | val;
	change = val != oval;
	if (change)
		snd_sbmixer_write(sb, reg, val);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}

/*
 * Double channel mixer element
 */

#ifdef TARGET_OS2
#define SB_DOUBLE(xname, left_reg, right_reg, left_shift, right_shift, mask) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0,0,\
  xname, 0,0,\
  snd_sbmixer_info_double, \
  snd_sbmixer_get_double, snd_sbmixer_put_double, \
  left_reg | (right_reg << 8) | (left_shift << 16) | (right_shift << 19) | (mask << 24) }
#else
#define SB_DOUBLE(xname, left_reg, right_reg, left_shift, right_shift, mask) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, \
  name: xname, \
  info: snd_sbmixer_info_double, \
  get: snd_sbmixer_get_double, put: snd_sbmixer_put_double, \
  private_value: left_reg | (right_reg << 8) | (left_shift << 16) | (right_shift << 19) | (mask << 24) }
#endif

static int snd_sbmixer_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_sbmixer_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	sb_t *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x07;
	int right_shift = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	unsigned char left, right;

	spin_lock_irqsave(&sb->mixer_lock, flags);
	left = (snd_sbmixer_read(sb, left_reg) >> left_shift) & mask;
	right = (snd_sbmixer_read(sb, right_reg) >> right_shift) & mask;
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	ucontrol->value.integer.value[0] = left;
	ucontrol->value.integer.value[1] = right;
	return 0;
}

static int snd_sbmixer_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	sb_t *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x07;
	int right_shift = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int change;
	unsigned char left, right, oleft, oright;

	left = (ucontrol->value.integer.value[0] & mask) << left_shift;
	right = (ucontrol->value.integer.value[1] & mask) << right_shift;
	spin_lock_irqsave(&sb->mixer_lock, flags);
	if (left_reg == right_reg) {
		oleft = snd_sbmixer_read(sb, left_reg);
		left = (oleft & ~((mask << left_shift) | (mask << right_shift))) | left | right;
		change = left != oleft;
		if (change)
			snd_sbmixer_write(sb, left_reg, left);
	} else {
		oleft = snd_sbmixer_read(sb, left_reg);
		oright = snd_sbmixer_read(sb, right_reg);
		left = (oleft & ~(mask << left_shift)) | left;
		right = (oright & ~(mask << right_shift)) | right;
		change = left != oleft || right != oright;
		if (change) {
			snd_sbmixer_write(sb, left_reg, left);
			snd_sbmixer_write(sb, right_reg, right);
		}
	}
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}

/*
 * SBPRO input multiplexer
 */

static int snd_sb8mixer_info_mux(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[3] = {
		"Mic", "CD", "Line"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}


static int snd_sb8mixer_get_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	sb_t *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char oval;
	
	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, SB_DSP_CAPTURE_SOURCE);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	switch ((oval >> 0x01) & 0x03) {
	case SB_DSP_MIXS_CD:
		ucontrol->value.enumerated.item[0] = 1;
		break;
	case SB_DSP_MIXS_LINE:
		ucontrol->value.enumerated.item[0] = 2;
		break;
	default:
		ucontrol->value.enumerated.item[0] = 0;
		break;
	}
	return 0;
}

static int snd_sb8mixer_put_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	sb_t *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned char nval, oval;
	
	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	switch (ucontrol->value.enumerated.item[0]) {
	case 1:
		nval = SB_DSP_MIXS_CD;
		break;
	case 2:
		nval = SB_DSP_MIXS_LINE;
		break;
	default:
		nval = SB_DSP_MIXS_MIC;
	}
	nval <<= 1;
	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, SB_DSP_CAPTURE_SOURCE);
	nval |= oval & ~0x06;
	change = nval != oval;
	if (change)
		snd_sbmixer_write(sb, SB_DSP_CAPTURE_SOURCE, nval);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}

/*
 * SB16 input switch
 */

#ifdef TARGET_OS2
#define SB16_INPUT_SW(xname, reg1, reg2, left_shift, right_shift) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0,0,\
  xname, 0,0,\
  snd_sb16mixer_info_input_sw, \
  snd_sb16mixer_get_input_sw, snd_sb16mixer_put_input_sw, \
  reg1 | (reg2 << 8) | (left_shift << 16) | (right_shift << 24) }
#else
#define SB16_INPUT_SW(xname, reg1, reg2, left_shift, right_shift) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, \
  name: xname, \
  info: snd_sb16mixer_info_input_sw, \
  get: snd_sb16mixer_get_input_sw, put: snd_sb16mixer_put_input_sw, \
  private_value: reg1 | (reg2 << 8) | (left_shift << 16) | (right_shift << 24) }
#endif

static int snd_sb16mixer_info_input_sw(snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_sb16mixer_get_input_sw(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	sb_t *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg1 = kcontrol->private_value & 0xff;
	int reg2 = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x0f;
	int right_shift = (kcontrol->private_value >> 24) & 0x0f;
	unsigned char val1, val2;

	spin_lock_irqsave(&sb->mixer_lock, flags);
	val1 = snd_sbmixer_read(sb, reg1);
	val2 = snd_sbmixer_read(sb, reg2);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	ucontrol->value.integer.value[0] = (val1 >> left_shift) & 0x01;
	ucontrol->value.integer.value[1] = (val2 >> left_shift) & 0x01;
	ucontrol->value.integer.value[2] = (val1 >> right_shift) & 0x01;
	ucontrol->value.integer.value[3] = (val2 >> right_shift) & 0x01;
	return 0;
}                                                                                                                   

static int snd_sb16mixer_put_input_sw(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	sb_t *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg1 = kcontrol->private_value & 0xff;
	int reg2 = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x0f;
	int right_shift = (kcontrol->private_value >> 24) & 0x0f;
	int change;
	unsigned char val1, val2, oval1, oval2;

	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval1 = snd_sbmixer_read(sb, reg1);
	oval2 = snd_sbmixer_read(sb, reg2);
	val1 = oval1 & ~((1 << left_shift) | (1 << right_shift));
	val2 = oval2 & ~((1 << left_shift) | (1 << right_shift));
	val1 |= (ucontrol->value.integer.value[0] & 1) << left_shift;
	val2 |= (ucontrol->value.integer.value[1] & 1) << left_shift;
	val1 |= (ucontrol->value.integer.value[2] & 1) << right_shift;
	val2 |= (ucontrol->value.integer.value[3] & 1) << right_shift;
	change = val1 != oval1 || val2 != oval2;
	if (change) {
		snd_sbmixer_write(sb, reg1, val1);
		snd_sbmixer_write(sb, reg2, val2);
	}
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}

#define SB20_CONTROLS (sizeof(snd_sb20_controls)/sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_sb20_controls[] = {
SB_SINGLE("Master Playback Volume", SB_DSP20_MASTER_DEV, 1, 7),
SB_SINGLE("PCM Playback Volume", SB_DSP20_PCM_DEV, 1, 3),
SB_SINGLE("Synth Playback Volume", SB_DSP20_FM_DEV, 1, 7),
SB_SINGLE("CD Playback Volume", SB_DSP20_CD_DEV, 1, 7)
};

#define SB20_INIT_VALUES (sizeof(snd_sb20_init_values)/sizeof(unsigned char)/2)

static unsigned char snd_sb20_init_values[][2] = {
	{ SB_DSP20_MASTER_DEV, 0 },
	{ SB_DSP20_FM_DEV, 0 },
};

#define SBPRO_CONTROLS (sizeof(snd_sbpro_controls)/sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_sbpro_controls[] = {
SB_DOUBLE("Master Playback Volume", SB_DSP_MASTER_DEV, SB_DSP_MASTER_DEV, 5, 1, 7),
SB_DOUBLE("PCM Playback Volume", SB_DSP_PCM_DEV, SB_DSP_PCM_DEV, 5, 1, 7),
SB_SINGLE("PCM Playback Filter", SB_DSP_PLAYBACK_FILT, 5, 1),
SB_DOUBLE("Synth Playback Volume", SB_DSP_FM_DEV, SB_DSP_FM_DEV, 5, 1, 7),
SB_DOUBLE("CD Playback Volume", SB_DSP_CD_DEV, SB_DSP_CD_DEV, 5, 1, 7),
SB_DOUBLE("Line Playback Volume", SB_DSP_LINE_DEV, SB_DSP_LINE_DEV, 5, 1, 7),
SB_SINGLE("Mic Playback Volume", SB_DSP_MIC_DEV, 1, 3),
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Capture Source",0,0,
	snd_sb8mixer_info_mux,
	snd_sb8mixer_get_mux,
	snd_sb8mixer_put_mux,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Capture Source",
	info: snd_sb8mixer_info_mux,
	get: snd_sb8mixer_get_mux,
	put: snd_sb8mixer_put_mux,
#endif
},
SB_SINGLE("Capture Filter", SB_DSP_CAPTURE_FILT, 5, 1),
SB_SINGLE("Capture Low-Pass Filter", SB_DSP_CAPTURE_FILT, 3, 1)
};

#define SBPRO_INIT_VALUES (sizeof(snd_sbpro_init_values)/sizeof(unsigned char)/2)

static unsigned char snd_sbpro_init_values[][2] = {
	{ SB_DSP_MASTER_DEV, 0 },
	{ SB_DSP_PCM_DEV, 0 },
	{ SB_DSP_FM_DEV, 0 },
};

#define SB16_CONTROLS (sizeof(snd_sb16_controls)/sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_sb16_controls[] = {
SB_DOUBLE("Master Playback Volume", SB_DSP4_MASTER_DEV, (SB_DSP4_MASTER_DEV + 1), 3, 3, 31),
SB_SINGLE("3D Enhancement Switch", SB_DSP4_3DSE, 0, 1),
SB_DOUBLE("Tone Control - Bass", SB_DSP4_BASS_DEV, (SB_DSP4_BASS_DEV + 1), 4, 4, 15),
SB_DOUBLE("Tone Control - Treble", SB_DSP4_TREBLE_DEV, (SB_DSP4_TREBLE_DEV + 1), 4, 4, 15),
SB_DOUBLE("PCM Playback Volume", SB_DSP4_PCM_DEV, (SB_DSP4_PCM_DEV + 1), 3, 3, 31),
SB16_INPUT_SW("Synth Capture Route", SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, 6, 5),
SB_DOUBLE("Synth Playback Volume", SB_DSP4_SYNTH_DEV, (SB_DSP4_SYNTH_DEV + 1), 3, 3, 31),
SB16_INPUT_SW("CD Capture Route", SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, 2, 1),
SB_DOUBLE("CD Playback Switch", SB_DSP4_OUTPUT_SW, SB_DSP4_OUTPUT_SW, 2, 1, 1),
SB_DOUBLE("CD Playback Volume", SB_DSP4_CD_DEV, (SB_DSP4_CD_DEV + 1), 3, 3, 31),
SB16_INPUT_SW("Line Capture Route", SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, 4, 3),
SB_DOUBLE("Line Playback Switch", SB_DSP4_OUTPUT_SW, SB_DSP4_OUTPUT_SW, 4, 3, 1),
SB_DOUBLE("Line Playback Volume", SB_DSP4_LINE_DEV, (SB_DSP4_LINE_DEV + 1), 3, 3, 31),
SB_DOUBLE("Mic Capture Switch", SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, 0, 0, 1),
SB_SINGLE("Mic Playback Switch", SB_DSP4_OUTPUT_SW, 0, 1),
SB_SINGLE("Mic Playback Volume", SB_DSP4_MIC_DEV, 3, 31),
SB_SINGLE("PC Speaker Volume", SB_DSP4_SPEAKER_DEV, 6, 3),
SB_DOUBLE("Capture Volume", SB_DSP4_IGAIN_DEV, (SB_DSP4_IGAIN_DEV + 1), 6, 6, 3),
SB_DOUBLE("Playback Volume", SB_DSP4_OGAIN_DEV, (SB_DSP4_OGAIN_DEV + 1), 6, 6, 3),
SB_SINGLE("Auto Mic Gain", SB_DSP4_MIC_AGC, 0, 1)
};

#define SB16_INIT_VALUES (sizeof(snd_sb16_init_values)/sizeof(unsigned char)/2)

static unsigned char snd_sb16_init_values[][2] = {
	{ SB_DSP4_MASTER_DEV + 0, 0 },
	{ SB_DSP4_MASTER_DEV + 1, 0 },
	{ SB_DSP4_PCM_DEV + 0, 0 },
	{ SB_DSP4_PCM_DEV + 1, 0 },
	{ SB_DSP4_SYNTH_DEV + 0, 0 },
	{ SB_DSP4_SYNTH_DEV + 1, 0 },
	{ SB_DSP4_INPUT_LEFT, 0 },
	{ SB_DSP4_INPUT_RIGHT, 0 },
	{ SB_DSP4_OUTPUT_SW, 0 },
	{ SB_DSP4_SPEAKER_DEV, 0 },
};

static int snd_sbmixer_init(sb_t *chip,
			    snd_kcontrol_new_t *controls,
			    int controls_count,
			    unsigned char map[][2],
			    int map_count,
			    char *name)
{
	unsigned long flags;
	snd_card_t *card = chip->card;
	int idx, err;

	/* mixer reset */
	spin_lock_irqsave(&chip->mixer_lock, flags);
	snd_sbmixer_write(chip, 0x00, 0x00);
	spin_unlock_irqrestore(&chip->mixer_lock, flags);

	/* mute and zero volume channels */
	for (idx = 0; idx < map_count; idx++) {
		spin_lock_irqsave(&chip->mixer_lock, flags);
		snd_sbmixer_write(chip, map[idx][0], map[idx][1]);
		spin_unlock_irqrestore(&chip->mixer_lock, flags);
	}

	for (idx = 0; idx < controls_count; idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&controls[idx], chip))) < 0)
			return err;
	}
	snd_component_add(card, name);
	strcpy(card->mixername, name);
	return 0;
}

int snd_sbmixer_new(sb_t *chip)
{
	snd_card_t * card;
	int err;

	snd_assert(chip != NULL && chip->card != NULL, return -EINVAL);

	card = chip->card;

	switch (chip->hardware) {
	case SB_HW_10:
		return 0; /* no mixer chip on SB1.x */
	case SB_HW_20:
	case SB_HW_201:
		if ((err = snd_sbmixer_init(chip,
					    snd_sb20_controls, SB20_CONTROLS,
					    snd_sb20_init_values, SB20_INIT_VALUES,
					    "CTL1335")) < 0)
			return err;
		break;
	case SB_HW_PRO:
		if ((err = snd_sbmixer_init(chip,
					    snd_sbpro_controls, SBPRO_CONTROLS,
					    snd_sbpro_init_values, SBPRO_INIT_VALUES,
					    "CTL1345")) < 0)
			return err;
		break;
	case SB_HW_16:
	case SB_HW_ALS100:
	case SB_HW_ALS4000:
		if ((err = snd_sbmixer_init(chip,
					    snd_sb16_controls, SB16_CONTROLS,
					    snd_sb16_init_values, SB16_INIT_VALUES,
					    "CTL1745")) < 0)
			return err;
		break;
	default:
		strcpy(card->mixername, "???");
	}
	return 0;
}
