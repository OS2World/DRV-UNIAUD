/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com).
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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Universal interface for Audio Codec '97");
MODULE_LICENSE("GPL");

static int enable_loopback;

MODULE_PARM(enable_loopback, "i");
MODULE_PARM_DESC(enable_loopback, "Enable AC97 ADC/DAC Loopback Control");
MODULE_PARM_SYNTAX(enable_loopback, SNDRV_BOOLEAN_FALSE_DESC);

#define chip_t ac97_t

/*

 */

static void snd_ac97_proc_init(snd_card_t * card, ac97_t * ac97);
static void snd_ac97_proc_done(ac97_t * ac97);

static int patch_wolfson00(ac97_t * ac97);
static int patch_wolfson03(ac97_t * ac97);
static int patch_wolfson04(ac97_t * ac97);
static int patch_tritech_tr28028(ac97_t * ac97);
static int patch_sigmatel_stac9708(ac97_t * ac97);
static int patch_sigmatel_stac9721(ac97_t * ac97);
static int patch_sigmatel_stac9744(ac97_t * ac97);
static int patch_sigmatel_stac9756(ac97_t * ac97);
static int patch_cirrus_cs4299(ac97_t * ac97);
static int patch_cirrus_spdif(ac97_t * ac97);
static int patch_ad1819(ac97_t * ac97);
static int patch_ad1881(ac97_t * ac97);
static int patch_ad1886(ac97_t * ac97);

typedef struct {
	unsigned int id;
	unsigned int mask;
	char *name;
	int (*patch)(ac97_t *ac97);
} ac97_codec_id_t;

static const ac97_codec_id_t snd_ac97_codec_id_vendors[] = {
{ 0x414b4d00, 0xffffff00, "Asahi Kasei",	NULL },
{ 0x41445300, 0xffffff00, "Analog Devices",	NULL },
{ 0x414c4300, 0xffffff00, "Realtek",		NULL },
{ 0x414c4700, 0xffffff00, "Avance Logic",	NULL },
{ 0x43525900, 0xffffff00, "Cirrus Logic",	NULL },
{ 0x48525300, 0xffffff00, "Intersil",		NULL },
{ 0x49434500, 0xffffff00, "ICEnsemble",		NULL },
{ 0x4e534300, 0xffffff00, "National Semiconductor", NULL },
{ 0x53494c00, 0xffffff00, "Silicon Laboratory",	NULL },
{ 0x54524100, 0xffffff00, "TriTech",		NULL },
{ 0x54584e00, 0xffffff00, "Texas Instruments",	NULL },
{ 0x57454300, 0xffffff00, "Winbond",		NULL },
{ 0x574d4c00, 0xffffff00, "Wolfson",		NULL },
{ 0x594d4800, 0xffffff00, "Yamaha",		NULL },
{ 0x83847600, 0xffffff00, "SigmaTel",		NULL },
{ 0x45838300, 0xffffff00, "ESS Technology",	NULL },
{ 0,	      0, 	  NULL,			NULL }
};

static const ac97_codec_id_t snd_ac97_codec_ids[] = {
{ 0x414b4d00, 0xffffffff, "AK4540",		NULL },
{ 0x414b4d01, 0xffffffff, "AK4542",		NULL },
{ 0x414b4d02, 0xffffffff, "AK4543",		NULL },
{ 0x414b4d06, 0xffffffff, "AK4544A",		NULL },
{ 0x414b4d07, 0xffffffff, "AK4545",		NULL },
{ 0x41445303, 0xffffffff, "AD1819",		patch_ad1819 },
{ 0x41445340, 0xffffffff, "AD1881",		patch_ad1881 },
{ 0x41445348, 0xffffffff, "AD1881A",		patch_ad1881 },
{ 0x41445360, 0xffffffff, "AD1885",		patch_ad1881 },
{ 0x41445361, 0xffffffff, "AD1886",		patch_ad1886 },
{ 0x41445362, 0xffffffff, "AD1887",		patch_ad1881 },
{ 0x41445363, 0xffffffff, "AD1886A",		patch_ad1881 },
{ 0x41445372, 0xffffffff, "AD1981A",		patch_ad1881 },
{ 0x414c4300, 0xfffffff0, "RL5306",	 	NULL },
{ 0x414c4310, 0xfffffff0, "RL5382", 		NULL },
{ 0x414c4320, 0xfffffff0, "RL5383", 		NULL },
{ 0x414c4710, 0xfffffff0, "ALC200/200P",	NULL },
{ 0x414c4720, 0xfffffff0, "ALC650",		NULL },
{ 0x414c4730, 0xffffffff, "ALC101",		NULL },
{ 0x414c4740, 0xfffffff0, "ALC202",		NULL },
{ 0x414c4750, 0xfffffff0, "ALC250",		NULL },
{ 0x43525900, 0xfffffff8, "CS4297",		NULL },
{ 0x43525910, 0xfffffff8, "CS4297A",		patch_cirrus_spdif },
{ 0x42525920, 0xfffffff8, "CS4294/4298",	NULL },
{ 0x42525928, 0xfffffff8, "CS4294",		NULL },
{ 0x43525930, 0xfffffff8, "CS4299",		patch_cirrus_cs4299 },
{ 0x43525948, 0xfffffff8, "CS4201",		NULL },
{ 0x43525958, 0xfffffff8, "CS4205",		patch_cirrus_spdif },
{ 0x43525960, 0xfffffff8, "CS4291",		NULL },
{ 0x48525300, 0xffffff00, "HMP9701",		NULL },
{ 0x49434501, 0xffffffff, "ICE1230",		NULL },
{ 0x49434511, 0xffffffff, "ICE1232",		NULL }, // alias VIA VT1611A?
{ 0x4e534300, 0xffffffff, "LM4540/43/45/46/48",	NULL }, // only guess --jk
{ 0x4e534331, 0xffffffff, "LM4549",		NULL },
{ 0x53494c22, 0xffffffff, "Si3036",		NULL },
{ 0x53494c23, 0xffffffff, "Si3038",		NULL },
{ 0x54524108, 0xffffffff, "TR28028",		patch_tritech_tr28028 }, // added by xin jin [07/09/99]
{ 0x54524123, 0xffffffff, "TR28602",		NULL }, // only guess --jk [TR28023 = eMicro EM28023 (new CT1297)]
{ 0x54584e20, 0xffffffff, "TLC320AD9xC",	NULL },
{ 0x57454301, 0xffffffff, "W83971D",		NULL },
{ 0x574d4c00, 0xffffffff, "WM9701A",		patch_wolfson00 },
{ 0x574d4c03, 0xffffffff, "WM9703/9707",	patch_wolfson03 },
{ 0x574d4c04, 0xffffffff, "WM9704 (quad)",	patch_wolfson04 },
{ 0x594d4800, 0xffffffff, "YMF743",		NULL },
{ 0x83847600, 0xffffffff, "STAC9700/83/84",	NULL },
{ 0x83847604, 0xffffffff, "STAC9701/3/4/5",	NULL },
{ 0x83847605, 0xffffffff, "STAC9704",		NULL },
{ 0x83847608, 0xffffffff, "STAC9708/11",	patch_sigmatel_stac9708 },
{ 0x83847609, 0xffffffff, "STAC9721/23",	patch_sigmatel_stac9721 },
{ 0x83847644, 0xffffffff, "STAC9744",		patch_sigmatel_stac9744 },
{ 0x83847656, 0xffffffff, "STAC9756/57",	patch_sigmatel_stac9756 },
{ 0x45838308, 0xffffffff, "ESS1988",		NULL },
{ 0, 	      0,	  NULL,			NULL }
};

#define AC97_ID_AK4540		0x414b4d00
#define AC97_ID_AK4542		0x414b4d01
#define AC97_ID_AD1819		0x41445303
#define AC97_ID_AD1881		0x41445340
#define AC97_ID_AD1881A		0x41445348
#define AC97_ID_AD1885		0x41445360
#define AC97_ID_AD1886		0x41445361
#define AC97_ID_AD1887		0x41445362
#define AC97_ID_TR28028		0x54524108
#define AC97_ID_STAC9700	0x83847600
#define AC97_ID_STAC9704	0x83847604
#define AC97_ID_STAC9705	0x83847605
#define AC97_ID_STAC9708	0x83847608
#define AC97_ID_STAC9721	0x83847609
#define AC97_ID_STAC9744	0x83847644
#define AC97_ID_STAC9756	0x83847656
#define AC97_ID_CS4297A		0x43525910
#define AC97_ID_CS4299		0x43525930
#define AC97_ID_CS4201		0x43525948
#define AC97_ID_CS4205		0x43525958

static const char *snd_ac97_stereo_enhancements[] =
{
  /*   0 */ "No 3D Stereo Enhancement",
  /*   1 */ "Analog Devices Phat Stereo",
  /*   2 */ "Creative Stereo Enhancement",
  /*   3 */ "National Semi 3D Stereo Enhancement",
  /*   4 */ "YAMAHA Ymersion",
  /*   5 */ "BBE 3D Stereo Enhancement",
  /*   6 */ "Crystal Semi 3D Stereo Enhancement",
  /*   7 */ "Qsound QXpander",
  /*   8 */ "Spatializer 3D Stereo Enhancement",
  /*   9 */ "SRS 3D Stereo Enhancement",
  /*  10 */ "Platform Tech 3D Stereo Enhancement",
  /*  11 */ "AKM 3D Audio",
  /*  12 */ "Aureal Stereo Enhancement",
  /*  13 */ "Aztech 3D Enhancement",
  /*  14 */ "Binaura 3D Audio Enhancement",
  /*  15 */ "ESS Technology Stereo Enhancement",
  /*  16 */ "Harman International VMAx",
  /*  17 */ "Nvidea 3D Stereo Enhancement",
  /*  18 */ "Philips Incredible Sound",
  /*  19 */ "Texas Instruments 3D Stereo Enhancement",
  /*  20 */ "VLSI Technology 3D Stereo Enhancement",
  /*  21 */ "TriTech 3D Stereo Enhancement",
  /*  22 */ "Realtek 3D Stereo Enhancement",
  /*  23 */ "Samsung 3D Stereo Enhancement",
  /*  24 */ "Wolfson Microelectronics 3D Enhancement",
  /*  25 */ "Delta Integration 3D Enhancement",
  /*  26 */ "SigmaTel 3D Enhancement",
  /*  27 */ "Reserved 27",
  /*  28 */ "Rockwell 3D Stereo Enhancement",
  /*  29 */ "Reserved 29",
  /*  30 */ "Reserved 30",
  /*  31 */ "Reserved 31"
};

/*
 *  I/O routines
 */

static int snd_ac97_valid_reg(ac97_t *ac97, unsigned short reg)
{
	/* filter some registers for buggy codecs */
	switch (ac97->id) {
	case AC97_ID_AK4540:
	case AC97_ID_AK4542:
		if (reg <= 0x1c || reg == 0x20 || reg == 0x26 || reg >= 0x7c)
			return 1;
		return 0;
	case AC97_ID_AD1819:	/* AD1819 */
	case AC97_ID_AD1881:	/* AD1881 */
	case AC97_ID_AD1881A:	/* AD1881A */
		if (reg >= 0x3a && reg <= 0x6e)	/* 0x59 */
			return 0;
		return 1;
	case AC97_ID_AD1885:	/* AD1885 */
	case AC97_ID_AD1886:	/* AD1886 */
	case AC97_ID_AD1887:	/* AD1887 - !!verify!! --jk */
		if (reg == 0x5a)
			return 1;
		if (reg >= 0x3c && reg <= 0x6e)	/* 0x59 */
			return 0;
		return 1;
	case AC97_ID_STAC9700:
	case AC97_ID_STAC9704:
	case AC97_ID_STAC9705:
	case AC97_ID_STAC9708:
	case AC97_ID_STAC9721:
	case AC97_ID_STAC9744:
	case AC97_ID_STAC9756:
		if (reg <= 0x3a || reg >= 0x5a)
			return 1;
		return 0;
	}
	return 1;
}

void snd_ac97_write(ac97_t *ac97, unsigned short reg, unsigned short value)
{
	if (!snd_ac97_valid_reg(ac97, reg))
		return;
	ac97->write(ac97, reg, value);
}

unsigned short snd_ac97_read(ac97_t *ac97, unsigned short reg)
{
	if (!snd_ac97_valid_reg(ac97, reg))
		return 0;
	return ac97->read(ac97, reg);
}

void snd_ac97_write_cache(ac97_t *ac97, unsigned short reg, unsigned short value)
{
	if (!snd_ac97_valid_reg(ac97, reg))
		return;
	spin_lock(&ac97->reg_lock);
	ac97->write(ac97, reg, ac97->regs[reg] = value);
	spin_unlock(&ac97->reg_lock);
	set_bit(reg, &ac97->reg_accessed);
}

#ifndef CONFIG_SND_DEBUG
#define snd_ac97_write_cache_test snd_ac97_write_cache
#else
static void snd_ac97_write_cache_test(ac97_t *ac97, unsigned short reg, unsigned short value)
{
	return snd_ac97_write_cache(ac97, reg, value);
	if (!snd_ac97_valid_reg(ac97, reg))
		return;
	spin_lock(&ac97->reg_lock);
	ac97->write(ac97, reg, value);
	ac97->regs[reg] = ac97->read(ac97, reg);
	if (value != ac97->regs[reg])
		snd_printk("AC97 reg=%02x val=%04x real=%04x\n", reg, value, ac97->regs[reg]);
	spin_unlock(&ac97->reg_lock);
}
#endif

int snd_ac97_update(ac97_t *ac97, unsigned short reg, unsigned short value)
{
	int change;

	if (!snd_ac97_valid_reg(ac97, reg))
		return -EINVAL;
	spin_lock(&ac97->reg_lock);
	change = ac97->regs[reg] != value;
	if (change) {
		ac97->write(ac97, reg, value);
		ac97->regs[reg] = value;
	}
	spin_unlock(&ac97->reg_lock);
	return change;
}

int snd_ac97_update_bits(ac97_t *ac97, unsigned short reg, unsigned short mask, unsigned short value)
{
	int change;
	unsigned short old, new;

	if (!snd_ac97_valid_reg(ac97, reg))
		return -EINVAL;
	spin_lock(&ac97->reg_lock);
	old = ac97->regs[reg];
	new = (old & ~mask) | value;
	change = old != new;
	if (change) {
		ac97->write(ac97, reg, new);
		ac97->regs[reg] = new;
	}
	spin_unlock(&ac97->reg_lock);
	return change;
}

int snd_ac97_ad18xx_update_pcm_bits(ac97_t *ac97, int codec, unsigned short mask, unsigned short value)
{
	int change;
	unsigned short old, new;

	down(&ac97->spec.ad18xx.mutex);
	spin_lock(&ac97->reg_lock);
	old = ac97->spec.ad18xx.pcmreg[codec];
	new = (old & ~mask) | value;
	change = old != new;
	if (change) {
		/* select single codec */
		ac97->write(ac97, AC97_AD_SERIAL_CFG, ac97->spec.ad18xx.unchained[codec] | ac97->spec.ad18xx.chained[codec]);
		/* update PCM bits */
		ac97->write(ac97, AC97_PCM, new);
		/* select all codecs */
		ac97->write(ac97, AC97_AD_SERIAL_CFG, 0x7000);
		ac97->spec.ad18xx.pcmreg[codec] = new;
	}
	spin_unlock(&ac97->reg_lock);
	up(&ac97->spec.ad18xx.mutex);
	return change;
}

/*
 *
 */

static int snd_ac97_info_mux(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[8] = {
		"Mic", "CD", "Video", "Aux", "Line",
		"Mix", "Mix Mono", "Phone"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 2;
	uinfo->value.enumerated.items = 8;
	if (uinfo->value.enumerated.item > 7)
		uinfo->value.enumerated.item = 7;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_get_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	
	val = ac97->regs[AC97_REC_SEL];
	ucontrol->value.enumerated.item[0] = (val >> 8) & 7;
	ucontrol->value.enumerated.item[1] = (val >> 0) & 7;
	return 0;
}

static int snd_ac97_put_mux(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	
	if (ucontrol->value.enumerated.item[0] > 7 ||
	    ucontrol->value.enumerated.item[1] > 7)
		return -EINVAL;
	val = (ucontrol->value.enumerated.item[0] << 8) |
	      (ucontrol->value.enumerated.item[1] << 0);
	return snd_ac97_update(ac97, AC97_REC_SEL, val);
}

#ifdef TARGET_OS2
#define AC97_ENUM_DOUBLE(xname, reg, shift, invert) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, 0, 0, snd_ac97_info_enum_double, \
  snd_ac97_get_enum_double, snd_ac97_put_enum_double, \
  reg | (shift << 8) | (invert << 24) }
#else
#define AC97_ENUM_DOUBLE(xname, reg, shift, invert) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, info: snd_ac97_info_enum_double, \
  get: snd_ac97_get_enum_double, put: snd_ac97_put_enum_double, \
  private_value: reg | (shift << 8) | (invert << 24) }
#endif

static int snd_ac97_info_enum_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts1[2] = { "pre 3D", "post 3D" };
	static char *texts2[2] = { "Mix", "Mic" };
	static char *texts3[2] = { "Mic1", "Mic2" };
	char **texts = NULL;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;

	switch (reg) {
	case AC97_GENERAL_PURPOSE:
		switch (shift) {
		case 15: texts = texts1; break;
		case 9: texts = texts2; break;
		case 8: texts = texts3; break;
		}
	}
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ac97_get_enum_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	val = (ac97->regs[reg] >> shift) & 1;
	if (invert)
		val ^= 1;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int snd_ac97_put_enum_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	if (ucontrol->value.enumerated.item[0] > 1)
		return -EINVAL;
	val = !!ucontrol->value.enumerated.item[0];
	if (invert)
		val = !val;
	return snd_ac97_update_bits(ac97, reg, 1 << shift, val << shift);
}

#ifdef TARGET_OS2
#define AC97_SINGLE(xname, reg, shift, mask, invert) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, 0, 0, snd_ac97_info_single, \
  snd_ac97_get_single, snd_ac97_put_single, \
  reg | (shift << 8) | (mask << 16) | (invert << 24) }
#else
#define AC97_SINGLE(xname, reg, shift, mask, invert) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, info: snd_ac97_info_single, \
  get: snd_ac97_get_single, put: snd_ac97_put_single, \
  private_value: reg | (shift << 8) | (mask << 16) | (invert << 24) }
#endif

static int snd_ac97_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ac97_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	ucontrol->value.integer.value[0] = (ac97->regs[reg] >> shift) & mask;
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

static int snd_ac97_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	unsigned short val;
	
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	return snd_ac97_update_bits(ac97, reg, mask << shift, val << shift);
}

#ifdef TARGET_OS2
#define AC97_DOUBLE(xname, reg, shift_left, shift_right, mask, invert) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, (xname), 0, 0, snd_ac97_info_double, \
  snd_ac97_get_double, snd_ac97_put_double, \
  reg | (shift_left << 8) | (shift_right << 12) | (mask << 16) | (invert << 24) }
#else
#define AC97_DOUBLE(xname, reg, shift_left, shift_right, mask, invert) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: (xname), info: snd_ac97_info_double, \
  get: snd_ac97_get_double, put: snd_ac97_put_double, \
  private_value: reg | (shift_left << 8) | (shift_right << 12) | (mask << 16) | (invert << 24) }
#endif

static int snd_ac97_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ac97_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift_left = (kcontrol->private_value >> 8) & 0x0f;
	int shift_right = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	spin_lock(&ac97->reg_lock);
	ucontrol->value.integer.value[0] = (ac97->regs[reg] >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (ac97->regs[reg] >> shift_right) & mask;
	spin_unlock(&ac97->reg_lock);
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

static int snd_ac97_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift_left = (kcontrol->private_value >> 8) & 0x0f;
	int shift_right = (kcontrol->private_value >> 12) & 0x0f;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	unsigned short val1, val2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	return snd_ac97_update_bits(ac97, reg, 
				    (mask << shift_left) | (mask << shift_right),
				    (val1 << shift_left) | (val2 << shift_right));
}

static const snd_kcontrol_new_t snd_ac97_controls_master[2] = {
AC97_SINGLE("Master Playback Switch", AC97_MASTER, 15, 1, 1),
AC97_DOUBLE("Master Playback Volume", AC97_MASTER, 8, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_headphone[2] = {
AC97_SINGLE("Headphone Playback Switch", AC97_HEADPHONE, 15, 1, 1),
AC97_DOUBLE("Headphone Playback Volume", AC97_HEADPHONE, 8, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_master_mono[2] = {
AC97_SINGLE("Master Mono Playback Switch", AC97_MASTER_MONO, 15, 1, 1),
AC97_SINGLE("Master Mono Playback Volume", AC97_MASTER_MONO, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_tone[2] = {
AC97_SINGLE("Tone Control - Bass", AC97_MASTER_TONE, 8, 15, 1),
AC97_SINGLE("Tone Control - Treble", AC97_MASTER_TONE, 0, 15, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_pc_beep[2] = {
AC97_SINGLE("PC Speaker Playback Switch", AC97_PC_BEEP, 15, 1, 1),
AC97_SINGLE("PC Speaker Playback Volume", AC97_PC_BEEP, 1, 15, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_phone[2] = {
AC97_SINGLE("Phone Playback Switch", AC97_PHONE, 15, 1, 1),
AC97_SINGLE("Phone Playback Volume", AC97_PHONE, 0, 15, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_mic[3] = {
AC97_SINGLE("Mic Playback Switch", AC97_MIC, 15, 1, 1),
AC97_SINGLE("Mic Playback Volume", AC97_MIC, 0, 15, 1),
AC97_SINGLE("Mic Boost (+20dB)", AC97_MIC, 6, 1, 0)
};

static const snd_kcontrol_new_t snd_ac97_controls_line[2] = {
AC97_SINGLE("Line Playback Switch", AC97_LINE, 15, 1, 1),
AC97_DOUBLE("Line Playback Volume", AC97_LINE, 8, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_cd[2] = {
AC97_SINGLE("CD Playback Switch", AC97_CD, 15, 1, 1),
AC97_DOUBLE("CD Playback Volume", AC97_CD, 8, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_video[2] = {
AC97_SINGLE("Video Playback Switch", AC97_VIDEO, 15, 1, 1),
AC97_DOUBLE("Video Playback Volume", AC97_VIDEO, 8, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_aux[2] = {
AC97_SINGLE("Aux Playback Switch", AC97_AUX, 15, 1, 1),
AC97_DOUBLE("Aux Playback Volume", AC97_AUX, 8, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_pcm[2] = {
AC97_SINGLE("PCM Playback Switch", AC97_PCM, 15, 1, 1),
AC97_DOUBLE("PCM Playback Volume", AC97_PCM, 8, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_capture[3] = {
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0,
	"Capture Source", 0, 0, 
	snd_ac97_info_mux,
	snd_ac97_get_mux,
	snd_ac97_put_mux, 0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Capture Source",
	info: snd_ac97_info_mux,
	get: snd_ac97_get_mux,
	put: snd_ac97_put_mux,
#endif
},
AC97_SINGLE("Capture Switch", AC97_REC_GAIN, 15, 1, 1),
AC97_DOUBLE("Capture Volume", AC97_REC_GAIN, 8, 0, 15, 0)
};

static const snd_kcontrol_new_t snd_ac97_controls_mic_capture[2] = {
AC97_SINGLE("Mic Capture Switch", AC97_REC_GAIN_MIC, 15, 1, 1),
AC97_SINGLE("Mic Capture Volume", AC97_REC_GAIN_MIC, 0, 15, 0)
};

typedef enum {
	AC97_GENERAL_PCM_OUT = 0,
	AC97_GENERAL_STEREO_ENHANCEMENT,
	AC97_GENERAL_3D,
	AC97_GENERAL_LOUDNESS,
	AC97_GENERAL_MONO,
	AC97_GENERAL_MIC,
	AC97_GENERAL_LOOPBACK
} ac97_general_index_t;

static const snd_kcontrol_new_t snd_ac97_controls_general[7] = {
AC97_ENUM_DOUBLE("PCM Out Path & Mute", AC97_GENERAL_PURPOSE, 15, 0),
AC97_SINGLE("Simulated Stereo Enhancement", AC97_GENERAL_PURPOSE, 14, 1, 0),
AC97_SINGLE("3D Control - Switch", AC97_GENERAL_PURPOSE, 13, 1, 0),
AC97_SINGLE("Loudness (bass boost)", AC97_GENERAL_PURPOSE, 12, 1, 0),
AC97_ENUM_DOUBLE("Mono Output Select", AC97_GENERAL_PURPOSE, 9, 0),
AC97_ENUM_DOUBLE("Mic Select", AC97_GENERAL_PURPOSE, 8, 0),
AC97_SINGLE("ADC/DAC Loopback", AC97_GENERAL_PURPOSE, 7, 1, 0)
};

static const snd_kcontrol_new_t snd_ac97_controls_3d[2] = {
AC97_SINGLE("3D Control - Center", AC97_3D_CONTROL, 8, 15, 0),
AC97_SINGLE("3D Control - Depth", AC97_3D_CONTROL, 0, 15, 0)
};

static const snd_kcontrol_new_t snd_ac97_controls_center[2] = {
AC97_SINGLE("Center Playback Switch", AC97_CENTER_LFE_MASTER, 7, 1, 1),
AC97_SINGLE("Center Playback Volume", AC97_CENTER_LFE_MASTER, 0, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_lfe[2] = {
AC97_SINGLE("LFE Playback Switch", AC97_CENTER_LFE_MASTER, 15, 1, 1),
AC97_SINGLE("LFE Playback Volume", AC97_CENTER_LFE_MASTER, 8, 31, 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_surround[2] = {
AC97_DOUBLE("Surround Playback Switch", AC97_SURROUND_MASTER, 15, 7, 1, 1),
AC97_DOUBLE("Surround Playback Volume", AC97_SURROUND_MASTER, 8, 0, 31, 1),
};

static const snd_kcontrol_new_t snd_ac97_sigmatel_controls[] = {
AC97_SINGLE("Sigmatel DAC 6dB Attenuate", AC97_SIGMATEL_ANALOG, 1, 1, 0),
AC97_SINGLE("Sigmatel ADC 6dB Attenuate", AC97_SIGMATEL_ANALOG, 0, 1, 0)
};

static const snd_kcontrol_new_t snd_ac97_control_eapd =
AC97_SINGLE("External Amplifier Power Down", AC97_POWERDOWN, 15, 1, 0);

static int snd_ac97_spdif_mask_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}
                        
static int snd_ac97_spdif_cmask_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_CON_EMPHASIS_5015 |
					   IEC958_AES0_CON_NOT_COPYRIGHT;
	ucontrol->value.iec958.status[1] = IEC958_AES1_CON_CATEGORY |
					   IEC958_AES1_CON_ORIGINAL;
	ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS;
	return 0;
}
                        
static int snd_ac97_spdif_pmask_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	/* FIXME: AC'97 spec doesn't say which bits are used for what */
	ucontrol->value.iec958.status[0] = IEC958_AES0_PROFESSIONAL |
					   IEC958_AES0_NONAUDIO |
					   IEC958_AES0_PRO_FS |
					   IEC958_AES0_PRO_EMPHASIS_5015;
	return 0;
}

static int snd_ac97_spdif_default_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);

	spin_lock(&ac97->reg_lock);
	ucontrol->value.iec958.status[0] = ac97->spdif_status & 0xff;
	ucontrol->value.iec958.status[1] = (ac97->spdif_status >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (ac97->spdif_status >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (ac97->spdif_status >> 24) & 0xff;
	spin_unlock(&ac97->reg_lock);
	return 0;
}
                        
static int snd_ac97_spdif_default_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	unsigned int new = 0;
	unsigned short val = 0;
	int change = 0;

	spin_lock(&ac97->reg_lock);
	new = val = ucontrol->value.iec958.status[0] & (IEC958_AES0_PROFESSIONAL|IEC958_AES0_NONAUDIO);
	if (ucontrol->value.iec958.status[0] & IEC958_AES0_PROFESSIONAL) {
		new |= ucontrol->value.iec958.status[0] & (IEC958_AES0_PRO_FS|IEC958_AES0_PRO_EMPHASIS_5015);
		switch (new & IEC958_AES0_PRO_FS) {
		case IEC958_AES0_PRO_FS_44100: val |= 0<<12; break;
		case IEC958_AES0_PRO_FS_48000: val |= 2<<12; break;
		case IEC958_AES0_PRO_FS_32000: val |= 3<<12; break;
		default:		       val |= 1<<12; break;
		}
		if ((new & IEC958_AES0_PRO_EMPHASIS) == IEC958_AES0_PRO_EMPHASIS_5015)
			val |= 1<<3;
	} else {
		new |= ucontrol->value.iec958.status[0] & (IEC958_AES0_CON_EMPHASIS_5015|IEC958_AES0_CON_NOT_COPYRIGHT);
		new |= ((ucontrol->value.iec958.status[1] & (IEC958_AES1_CON_CATEGORY|IEC958_AES1_CON_ORIGINAL)) << 8);
		new |= ((ucontrol->value.iec958.status[3] & IEC958_AES3_CON_FS) << 24);
		if ((new & IEC958_AES0_CON_EMPHASIS) == IEC958_AES0_CON_EMPHASIS_5015)
			val |= 1<<3;
		if (!(new & IEC958_AES0_CON_NOT_COPYRIGHT))
			val |= 1<<2;
		val |= ((new >> 8) & 0xff) << 4;	// category + original
		switch ((new >> 24) & 0xff) {
		case IEC958_AES3_CON_FS_44100: val |= 0<<12; break;
		case IEC958_AES3_CON_FS_48000: val |= 2<<12; break;
		case IEC958_AES3_CON_FS_32000: val |= 3<<12; break;
		default:		       val |= 1<<12; break;
		}
	}

	

	if (ac97->flags & AC97_CS_SPDIF) {
		int x = (val >> 12) & 0x03;
		switch (x) {
		case 0: x = 1; break;  // 44.1
		case 2: x = 0; break;  // 48.0
		default: x = 0; break; // illegal.
		}
		change = snd_ac97_update_bits(ac97, AC97_CSR_SPDIF, 0x3fff, ((val & 0xcfff) | (x << 12)));
	} else {
		change = snd_ac97_update_bits(ac97, AC97_SPDIF, 0x3fff, val);
	}

	change |= ac97->spdif_status != new;
	ac97->spdif_status = new;
	spin_unlock(&ac97->reg_lock);
	return change;
}

static const snd_kcontrol_new_t snd_ac97_controls_spdif[5] = {
#ifdef TARGET_OS2
	{
		SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
		SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),0,
		SNDRV_CTL_ELEM_ACCESS_READ,
		snd_ac97_spdif_mask_info,
		snd_ac97_spdif_cmask_get,0,0
	},
	{
		SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),0,
		SNDRV_CTL_ELEM_ACCESS_READ,
		snd_ac97_spdif_mask_info,
		snd_ac97_spdif_pmask_get,0,0
	},
	{
		SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),0,0,
		snd_ac97_spdif_mask_info,
		snd_ac97_spdif_default_get,
		snd_ac97_spdif_default_put,0
	},
#else
	{
		access: SNDRV_CTL_ELEM_ACCESS_READ,
		iface: SNDRV_CTL_ELEM_IFACE_MIXER,
		name: SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
		info: snd_ac97_spdif_mask_info,
		get: snd_ac97_spdif_cmask_get,
	},
	{
		access: SNDRV_CTL_ELEM_ACCESS_READ,
		iface: SNDRV_CTL_ELEM_IFACE_MIXER,
		name: SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
		info: snd_ac97_spdif_mask_info,
		get: snd_ac97_spdif_pmask_get,
	},
	{
		iface: SNDRV_CTL_ELEM_IFACE_MIXER,
		name: SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
		info: snd_ac97_spdif_mask_info,
		get: snd_ac97_spdif_default_get,
		put: snd_ac97_spdif_default_put,
	},
#endif
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH),AC97_EXTENDED_STATUS, 2, 1, 0),
	AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "AC97-SPSA",AC97_EXTENDED_STATUS, 4, 3, 0)
};

static const snd_kcontrol_new_t snd_ac97_cirrus_controls_spdif[2] = {
    AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH), AC97_CSR_SPDIF, 15, 1, 0),
    AC97_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "AC97-SPSA", AC97_CSR_ACMODE, 0, 3, 0)
};

#ifdef TARGET_OS2
#define AD18XX_PCM_BITS(xname, codec, shift, mask) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, 0, 0, snd_ac97_ad18xx_pcm_info_bits, \
  snd_ac97_ad18xx_pcm_get_bits, snd_ac97_ad18xx_pcm_put_bits, \
  codec | (shift << 8) | (mask << 16) }
#else
#define AD18XX_PCM_BITS(xname, codec, shift, mask) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, info: snd_ac97_ad18xx_pcm_info_bits, \
  get: snd_ac97_ad18xx_pcm_get_bits, put: snd_ac97_ad18xx_pcm_put_bits, \
  private_value: codec | (shift << 8) | (mask << 16) }
#endif

static int snd_ac97_ad18xx_pcm_info_bits(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_ac97_ad18xx_pcm_get_bits(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	
	ucontrol->value.integer.value[0] = mask - ((ac97->spec.ad18xx.pcmreg[codec] >> shift) & mask);
	return 0;
}

static int snd_ac97_ad18xx_pcm_put_bits(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	unsigned short val;
	
	val = mask - (ucontrol->value.integer.value[0] & mask);
	return snd_ac97_ad18xx_update_pcm_bits(ac97, codec, mask << shift, val << shift);
}

#ifdef TARGET_OS2
#define AD18XX_PCM_VOLUME(xname, codec) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, 0, 0, snd_ac97_ad18xx_pcm_info_volume, \
  snd_ac97_ad18xx_pcm_get_volume, snd_ac97_ad18xx_pcm_put_volume, \
  codec }
#else
#define AD18XX_PCM_VOLUME(xname, codec) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, info: snd_ac97_ad18xx_pcm_info_volume, \
  get: snd_ac97_ad18xx_pcm_get_volume, put: snd_ac97_ad18xx_pcm_put_volume, \
  private_value: codec }
#endif

static int snd_ac97_ad18xx_pcm_info_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 31;
	return 0;
}

static int snd_ac97_ad18xx_pcm_get_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	
	spin_lock(&ac97->reg_lock);
	ucontrol->value.integer.value[0] = 31 - ((ac97->spec.ad18xx.pcmreg[codec] >> 0) & 31);
	ucontrol->value.integer.value[1] = 31 - ((ac97->spec.ad18xx.pcmreg[codec] >> 8) & 31);
	spin_unlock(&ac97->reg_lock);
	return 0;
}

static int snd_ac97_ad18xx_pcm_put_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ac97_t *ac97 = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->private_value & 3;
	unsigned short val1, val2;
	
	val1 = 31 - (ucontrol->value.integer.value[0] & 31);
	val2 = 31 - (ucontrol->value.integer.value[1] & 31);
	return snd_ac97_ad18xx_update_pcm_bits(ac97, codec, 0x1f1f, (val1 << 8) | val2);
}

static const snd_kcontrol_new_t snd_ac97_controls_ad18xx_pcm[2] = {
AD18XX_PCM_BITS("PCM Playback Switch", 0, 15, 1),
AD18XX_PCM_VOLUME("PCM Playback Volume", 0)
};

static const snd_kcontrol_new_t snd_ac97_controls_ad18xx_surround[2] = {
AD18XX_PCM_BITS("Surround Playback Switch", 1, 15, 1),
AD18XX_PCM_VOLUME("Surround Playback Volume", 1)
};

static const snd_kcontrol_new_t snd_ac97_controls_ad18xx_center[2] = {
AD18XX_PCM_BITS("Center Playback Switch", 2, 15, 1),
AD18XX_PCM_BITS("Center Playback Volume", 2, 8, 31)
};

static const snd_kcontrol_new_t snd_ac97_controls_ad18xx_lfe[1] = {
AD18XX_PCM_BITS("LFE Playback Volume", 2, 0, 31)
};

/*
 *
 */

static int snd_ac97_free(ac97_t *ac97)
{
	if (ac97) {
		snd_ac97_proc_done(ac97);
		if (ac97->private_free)
			ac97->private_free(ac97);
		snd_magic_kfree(ac97);
	}
	return 0;
}

static int snd_ac97_dev_free(snd_device_t *device)
{
	ac97_t *ac97 = snd_magic_cast(ac97_t, device->device_data, return -ENXIO);
	return snd_ac97_free(ac97);
}

static int snd_ac97_try_volume_mix(ac97_t * ac97, int reg)
{
	unsigned short val, mask = 0x8000;

	switch (reg) {
	case AC97_MASTER_TONE:
		return ac97->caps & 0x04 ? 1 : 0;
	case AC97_HEADPHONE:
		return ac97->caps & 0x10 ? 1 : 0;
	case AC97_REC_GAIN_MIC:
		return ac97->caps & 0x01 ? 1 : 0;
	case AC97_3D_CONTROL:
		if (ac97->caps & 0x7c00) {
			val = snd_ac97_read(ac97, reg);
			/* if nonzero - fixed and we can't set it */
			return val == 0;
		}
		return 0;
	case AC97_CENTER_LFE_MASTER:	/* center */
		if ((ac97->ext_id & 0x40) == 0)
			return 0;
		break;
	case AC97_CENTER_LFE_MASTER+1:	/* lfe */
		if ((ac97->ext_id & 0x100) == 0)
			return 0;
		reg = AC97_CENTER_LFE_MASTER;
		mask = 0x0080;
		break;
	case AC97_SURROUND_MASTER:
		if ((ac97->ext_id & 0x80) == 0)
			return 0;
		break;
	}
	val = snd_ac97_read(ac97, reg);
	if (!(val & mask)) {
		/* nothing seems to be here - mute flag is not set */
		/* try another test */
		snd_ac97_write_cache_test(ac97, reg, val | mask);
		val = snd_ac97_read(ac97, reg);
		if (!(val & mask))
			return 0;	/* nothing here */
	}
	return 1;		/* success, useable */
}

static int snd_ac97_try_bit(ac97_t * ac97, int reg, int bit)
{
	unsigned short mask, val, orig, res;

	mask = 1 << bit;
	orig = snd_ac97_read(ac97, reg);
	val = orig ^ mask;
	snd_ac97_write(ac97, reg, val);
	res = snd_ac97_read(ac97, reg);
	snd_ac97_write_cache(ac97, reg, orig);
	return res == val;
}

static void snd_ac97_change_volume_params1(ac97_t * ac97, int reg, unsigned char *max)
{
	unsigned short val, val1;

	*max = 63;
	val = 0x8000 | 0x0020;
	snd_ac97_write(ac97, reg, val);
	val1 = snd_ac97_read(ac97, reg);
	if (val != val1) {
		*max = 31;
	}
	/* reset volume to zero */
	snd_ac97_write_cache(ac97, reg, 0x8000);
}

static void snd_ac97_change_volume_params2(ac97_t * ac97, int reg, int shift, unsigned char *max)
{
	unsigned short val, val1;

	*max = 63;
	val = 0x8080 | (0x20 << shift);
	snd_ac97_write(ac97, reg, val);
	val1 = snd_ac97_read(ac97, reg);
	if (val != val1) {
		*max = 31;
	}
	/* reset volume to zero */
	snd_ac97_write_cache(ac97, reg, 0x8080);
}

static void snd_ac97_change_volume_params3(ac97_t * ac97, int reg, unsigned char *max)
{
	unsigned short val, val1;

	*max = 31;
	val = 0x8000 | 0x0010;
	snd_ac97_write(ac97, reg, val);
	val1 = snd_ac97_read(ac97, reg);
	if (val != val1) {
		*max = 15;
	}
	/* reset volume to zero */
	snd_ac97_write_cache(ac97, reg, 0x8000);
}

static inline int printable(unsigned int x)
{
	x &= 0xff;
	if (x < ' ' || x >= 0x71) {
		if (x <= 0x89)
			return x - 0x71 + 'A';
		return '?';
	}
	return x;
}

static snd_kcontrol_t *snd_ac97_cnew(const snd_kcontrol_new_t *_template, ac97_t * ac97)
{
	snd_kcontrol_new_t template;
	memcpy(&template, _template, sizeof(template));
	snd_runtime_check(!template.index, return NULL);
	template.index = ac97->num;
	return snd_ctl_new1(&template, ac97);
}

static int snd_ac97_mixer_build(snd_card_t * card, ac97_t * ac97)
{
	snd_kcontrol_t *kctl;
	int err, idx;
	unsigned char max;

	/* build master controls */
	/* AD claims to remove this control from AD1887, although spec v2.2 does not allow this */
	if (snd_ac97_try_volume_mix(ac97, AC97_MASTER)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_master[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_master[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params1(ac97, AC97_MASTER, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_MASTER, 0x8000 | max | (max << 8));
	}

	ac97->regs[AC97_CENTER_LFE_MASTER] = 0x8080;

	/* build center controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_CENTER_LFE_MASTER)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_center[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_center[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params2(ac97, AC97_CENTER_LFE_MASTER, 0, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_CENTER_LFE_MASTER, ac97->regs[AC97_CENTER_LFE_MASTER] | max);
	}

	/* build LFE controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_CENTER_LFE_MASTER+1)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_lfe[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_lfe[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params2(ac97, AC97_CENTER_LFE_MASTER, 8, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_CENTER_LFE_MASTER, ac97->regs[AC97_CENTER_LFE_MASTER] | max << 8);
	}

	/* build surround controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_SURROUND_MASTER)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_surround[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_surround[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params2(ac97, AC97_SURROUND_MASTER, 0, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_SURROUND_MASTER, 0x8080 | max | (max << 8));
	}

	/* build headphone controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_HEADPHONE)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_headphone[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_headphone[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params1(ac97, AC97_HEADPHONE, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_HEADPHONE, 0x8000 | max | (max << 8));
	}
	
	/* build master mono controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_MASTER_MONO)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_master_mono[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_master_mono[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params1(ac97, AC97_MASTER_MONO, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_MASTER_MONO, 0x8000 | max);
	}
	
	/* build master tone controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_MASTER_TONE)) {
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_tone[idx], ac97))) < 0)
				return err;
		snd_ac97_write_cache(ac97, AC97_MASTER_TONE, 0x0f0f);
	}
	
	/* build PC Speaker controls */
	if ((ac97->flags & AC97_HAS_PC_BEEP) ||
	    snd_ac97_try_volume_mix(ac97, AC97_PC_BEEP)) {
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_pc_beep[idx], ac97))) < 0)
				return err;
		snd_ac97_write_cache(ac97, AC97_PC_BEEP, 0x801e);
	}
	
	/* build Phone controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_PHONE)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_phone[0], ac97))) < 0)
			return err;
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_phone[1], ac97))) < 0)
			return err;
		snd_ac97_change_volume_params3(ac97, AC97_PHONE, &max);
		kctl->private_value &= ~(0xff << 16);
		kctl->private_value |= (int)max << 16;
		snd_ac97_write_cache(ac97, AC97_PHONE, 0x8000 | max);
	}
	
	/* build MIC controls */
	snd_ac97_change_volume_params3(ac97, AC97_MIC, &max);
	for (idx = 0; idx < 3; idx++) {
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_mic[idx], ac97))) < 0)
			return err;
		if (idx == 1) {		// volume
			kctl->private_value &= ~(0xff << 16);
			kctl->private_value |= (int)max << 16;
		}
	}
	snd_ac97_write_cache(ac97, AC97_MIC, 0x8000 | max);

	/* build Line controls */
	for (idx = 0; idx < 2; idx++)
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_line[idx], ac97))) < 0)
			return err;
	snd_ac97_write_cache(ac97, AC97_LINE, 0x9f1f);
	
	/* build CD controls */
	for (idx = 0; idx < 2; idx++)
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_cd[idx], ac97))) < 0)
			return err;
	snd_ac97_write_cache(ac97, AC97_CD, 0x9f1f);
	
	/* build Video controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_VIDEO)) {
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_video[idx], ac97))) < 0)
				return err;
		snd_ac97_write_cache(ac97, AC97_VIDEO, 0x9f1f);
	}

	/* build Aux controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_AUX)) {
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_aux[idx], ac97))) < 0)
				return err;
		snd_ac97_write_cache(ac97, AC97_AUX, 0x9f1f);
	}

	/* build PCM controls */
	if (ac97->flags & AC97_AD_MULTI) {
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_ad18xx_pcm[idx], ac97))) < 0)
				return err;
		ac97->spec.ad18xx.pcmreg[0] = 0x9f1f;
		if (ac97->scaps & AC97_SCAP_SURROUND_DAC) {
			for (idx = 0; idx < 2; idx++)
				if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_ad18xx_surround[idx], ac97))) < 0)
					return err;
			ac97->spec.ad18xx.pcmreg[1] = 0x9f1f;
		}
		if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC) {
			for (idx = 0; idx < 2; idx++)
				if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_ad18xx_center[idx], ac97))) < 0)
					return err;
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_ad18xx_lfe[0], ac97))) < 0)
				return err;
			ac97->spec.ad18xx.pcmreg[2] = 0x9f1f;
		}
	} else {
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_pcm[idx], ac97))) < 0)
				return err;
	}
	snd_ac97_write_cache(ac97, AC97_PCM, 0x9f1f);

	/* build Capture controls */
	for (idx = 0; idx < 3; idx++)
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_capture[idx], ac97))) < 0)
			return err;
	snd_ac97_write_cache(ac97, AC97_REC_SEL, 0x0000);
	snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x0000);

	/* build MIC Capture controls */
	if (snd_ac97_try_volume_mix(ac97, AC97_REC_GAIN_MIC)) {
		for (idx = 0; idx < 2; idx++)
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_mic_capture[idx], ac97))) < 0)
				return err;
		snd_ac97_write_cache(ac97, AC97_REC_GAIN_MIC, 0x0000);
	}

	/* build PCM out path & mute control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 15)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_PCM_OUT], ac97))) < 0)
			return err;
	}

	/* build Simulated Stereo Enhancement control */
	if (ac97->caps & 0x0008) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_STEREO_ENHANCEMENT], ac97))) < 0)
			return err;
	}

	/* build 3D Stereo Enhancement control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 13)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_3D], ac97))) < 0)
			return err;
	}

	/* build Loudness control */
	if (ac97->caps & 0x0020) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_LOUDNESS], ac97))) < 0)
			return err;
	}

	/* build Mono output select control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 9)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_MONO], ac97))) < 0)
			return err;
	}

	/* build Mic select control */
	if (snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 8)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_MIC], ac97))) < 0)
			return err;
	}

	/* build ADC/DAC loopback control */
	if (enable_loopback && snd_ac97_try_bit(ac97, AC97_GENERAL_PURPOSE, 7)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_general[AC97_GENERAL_LOOPBACK], ac97))) < 0)
			return err;
	}

	snd_ac97_write_cache(ac97, AC97_GENERAL_PURPOSE, 0x0000);

	/* build 3D controls */
	switch (ac97->id) {
	case AC97_ID_STAC9708:
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
			return err;
		strcpy(kctl->id.name, "3D Control Sigmatel - Depth");
		kctl->private_value = AC97_3D_CONTROL | (3 << 16);
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
			return err;
		strcpy(kctl->id.name, "3D Control Sigmatel - Rear Depth");
		kctl->private_value = AC97_3D_CONTROL | (2 << 8) | (3 << 16);
		snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
		break;
	case AC97_ID_STAC9700:
	case AC97_ID_STAC9721:
	case AC97_ID_STAC9744:
	case AC97_ID_STAC9756:
		if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
			return err;
		strcpy(kctl->id.name, "3D Control Sigmatel - Depth");
		kctl->private_value = AC97_3D_CONTROL | (3 << 16);
		snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
		break;
	default:
		if (snd_ac97_try_volume_mix(ac97, AC97_3D_CONTROL)) {
			unsigned short val;
			val = 0x0707;
			snd_ac97_write(ac97, AC97_3D_CONTROL, val);
			val = snd_ac97_read(ac97, AC97_3D_CONTROL);
			val = val == 0x0606;
			if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[0], ac97))) < 0)
				return err;
			if (val)
				kctl->private_value = AC97_3D_CONTROL | (9 << 8) | (7 << 16);
			if ((err = snd_ctl_add(card, kctl = snd_ac97_cnew(&snd_ac97_controls_3d[1], ac97))) < 0)
				return err;
			if (val)
				kctl->private_value = AC97_3D_CONTROL | (1 << 8) | (7 << 16);
			snd_ac97_write_cache(ac97, AC97_3D_CONTROL, 0x0000);
		}
	}
	
	/* build S/PDIF controls */
	if (ac97->ext_id & AC97_EA_SPDIF) {
		if (ac97->flags & AC97_CS_SPDIF) {
			for (idx = 0; idx < 3; idx++)
				if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_spdif[idx], ac97))) < 0)
					return err;
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_cirrus_controls_spdif[0], ac97))) < 0)
				return err;
			switch (ac97->id) {
			case AC97_ID_CS4205:
				if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_cirrus_controls_spdif[1], ac97))) < 0)
					return err;
				break;
			}
			/* set default PCM S/PDIF params */
			/* consumer,PCM audio,no copyright,no preemphasis,PCM coder,original,48000Hz */
			snd_ac97_write_cache(ac97, AC97_CSR_SPDIF, 0x0a20);
		} else {
			for (idx = 0; idx < 5; idx++)
				if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_controls_spdif[idx], ac97))) < 0)
					return err;
			/* set default PCM S/PDIF params */
			/* consumer,PCM audio,no copyright,no preemphasis,PCM coder,original,48000Hz */
			snd_ac97_write_cache(ac97, AC97_SPDIF, 0x2a20);
		}

		ac97->spdif_status = SNDRV_PCM_DEFAULT_CON_SPDIF;
	}
	
	/* build Sigmatel specific controls */
	switch (ac97->id) {
	case AC97_ID_STAC9700:
	case AC97_ID_STAC9708:
	case AC97_ID_STAC9721:
	case AC97_ID_STAC9744:
	case AC97_ID_STAC9756:
		snd_ac97_write_cache_test(ac97, AC97_SIGMATEL_ANALOG, snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG) & ~0x0003);
		if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_ANALOG, 1))
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_sigmatel_controls[0], ac97))) < 0)
				return err;
		if (snd_ac97_try_bit(ac97, AC97_SIGMATEL_ANALOG, 0))
			if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_sigmatel_controls[1], ac97))) < 0)
				return err;
	default:
		/* nothing */
		break;
	}

	if (snd_ac97_try_bit(ac97, AC97_POWERDOWN, 15)) {
		if ((err = snd_ctl_add(card, snd_ac97_cnew(&snd_ac97_control_eapd, ac97))) < 0)
			return err;
	}

	return 0;
}

static int snd_ac97_test_rate(ac97_t *ac97, int reg, int rate)
{
	unsigned short val;
	unsigned int tmp;

	tmp = ((unsigned int)rate * ac97->clock) / 48000;
	snd_ac97_write_cache_test(ac97, reg, tmp & 0xffff);
	val = snd_ac97_read(ac97, reg);
	return val == (tmp & 0xffff);
}

static void snd_ac97_determine_rates(ac97_t *ac97, int reg, unsigned int *r_result)
{
	unsigned int result = 0;

	/* test a non-standard rate */
	if (snd_ac97_test_rate(ac97, reg, 11000))
		result |= SNDRV_PCM_RATE_CONTINUOUS;
	/* let's try to obtain standard rates */
	if (snd_ac97_test_rate(ac97, reg, 8000))
		result |= SNDRV_PCM_RATE_8000;
	if (snd_ac97_test_rate(ac97, reg, 11025))
		result |= SNDRV_PCM_RATE_11025;
	if (snd_ac97_test_rate(ac97, reg, 16000))
		result |= SNDRV_PCM_RATE_16000;
	if (snd_ac97_test_rate(ac97, reg, 22050))
		result |= SNDRV_PCM_RATE_22050;
	if (snd_ac97_test_rate(ac97, reg, 32000))
		result |= SNDRV_PCM_RATE_32000;
	if (snd_ac97_test_rate(ac97, reg, 44100))
		result |= SNDRV_PCM_RATE_44100;
	if (snd_ac97_test_rate(ac97, reg, 48000))
		result |= SNDRV_PCM_RATE_48000;
	*r_result = result;
}

static void snd_ac97_get_name(ac97_t *ac97, unsigned int id, char *name)
{
	const ac97_codec_id_t *pid;

	sprintf(name, "0x%x %c%c%c", id,
		printable(id >> 24),
		printable(id >> 16),
		printable(id >> 8));
	for (pid = snd_ac97_codec_id_vendors; pid->id; pid++)
		if (pid->id == (id & pid->mask)) {
			strcpy(name, pid->name);
			if (ac97 && pid->patch)
				pid->patch(ac97);
			goto __vendor_ok;
		}
	return;

      __vendor_ok:
	for (pid = snd_ac97_codec_ids; pid->id; pid++)
		if (pid->id == (id & pid->mask)) {
			strcat(name, " ");
			strcat(name, pid->name);
			if (pid->mask != 0xffffffff)
				sprintf(name + strlen(name), " rev %d", id & ~pid->mask);
			if (ac97 && pid->patch)
				pid->patch(ac97);
			return;
		}
	sprintf(name + strlen(name), " (%x)", id & 0xff);
}

int snd_ac97_mixer(snd_card_t * card, ac97_t * _ac97, ac97_t ** rac97)
{
	int err;
	ac97_t *ac97;
	char name[64];
	signed long end_time;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_ac97_dev_free, 0, 0
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_ac97_dev_free,
	};
#endif
	snd_assert(rac97 != NULL, return -EINVAL);
	*rac97 = NULL;
	snd_assert(card != NULL && _ac97 != NULL, return -EINVAL);
	ac97 = snd_magic_kcalloc(ac97_t, 0, GFP_KERNEL);
	if (ac97 == NULL)
		return -ENOMEM;
	*ac97 = *_ac97;
	ac97->card = card;
	spin_lock_init(&ac97->reg_lock);
	snd_ac97_write(ac97, AC97_RESET, 0);	/* reset to defaults */
	if (ac97->wait)
		ac97->wait(ac97);
	else {
		udelay(50);

		/* it's necessary to wait awhile until registers are accessible after RESET */
		/* because the PCM or MASTER volume registers can be modified, */
		/* the REC_GAIN register is used for tests */
		end_time = jiffies + HZ;
		do {
			/* use preliminary reads to settle the communication */
			snd_ac97_read(ac97, AC97_RESET);
			snd_ac97_read(ac97, AC97_VENDOR_ID1);
			snd_ac97_read(ac97, AC97_VENDOR_ID2);
			/* test if we can write to the PCM volume register */
			snd_ac97_write_cache(ac97, AC97_REC_GAIN, 0x8a05);
			if ((err = snd_ac97_read(ac97, AC97_REC_GAIN)) == 0x8a05)
				goto __access_ok;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/100);
		} while (end_time - (signed long)jiffies >= 0);
		snd_printd("AC'97 %d:%d does not respond - RESET [REC_GAIN = 0x%x]\n", ac97->num, ac97->addr, err);
		snd_ac97_free(ac97);
		return -ENXIO;
	}
      __access_ok:
	ac97->caps = snd_ac97_read(ac97, AC97_RESET);
	ac97->id = snd_ac97_read(ac97, AC97_VENDOR_ID1) << 16;
	ac97->id |= snd_ac97_read(ac97, AC97_VENDOR_ID2);
	ac97->ext_id = snd_ac97_read(ac97, AC97_EXTENDED_ID);
	if (ac97->ext_id == 0xffff)	/* invalid combination */
		ac97->ext_id = 0;
	if (ac97->id == 0x00000000 || ac97->id == 0xffffffff) {
		snd_printk("AC'97 %d:%d access is not valid [0x%x], removing mixer.\n", ac97->num, ac97->addr, ac97->id);
		snd_ac97_free(ac97);
		return -EIO;
	}
	/* FIXME: add powerdown control */
	/* nothing should be in powerdown mode */
	snd_ac97_write_cache_test(ac97, AC97_POWERDOWN, 0);
	snd_ac97_write_cache_test(ac97, AC97_RESET, 0);		/* reset to defaults */
	udelay(100);
	/* nothing should be in powerdown mode */
	snd_ac97_write_cache_test(ac97, AC97_POWERDOWN, 0);
	snd_ac97_write_cache_test(ac97, AC97_GENERAL_PURPOSE, 0);
	end_time = jiffies + (HZ / 10);
	do {
		if ((snd_ac97_read(ac97, AC97_POWERDOWN) & 0x0f) == 0x0f)
			goto __ready_ok;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ/10);
	} while (end_time - (signed long)jiffies >= 0);
	snd_printk("AC'97 %d:%d analog subsections not ready\n", ac97->num, ac97->addr);
      __ready_ok:
	if (ac97->clock == 0)
		ac97->clock = 48000;	/* standard value */
	if (ac97->ext_id & 0x0189)	/* L/R, MIC, SDAC, LDAC VRA support */
		snd_ac97_write_cache(ac97, AC97_EXTENDED_STATUS, ac97->ext_id & 0x0189);
	if (ac97->ext_id & 0x0001) {	/* VRA support */
		snd_ac97_determine_rates(ac97, AC97_PCM_FRONT_DAC_RATE, &ac97->rates_front_dac);
		snd_ac97_determine_rates(ac97, AC97_PCM_LR_ADC_RATE, &ac97->rates_adc);
	} else {
		ac97->rates_front_dac = SNDRV_PCM_RATE_48000;
		ac97->rates_adc = SNDRV_PCM_RATE_48000;
	}
	if (ac97->ext_id & 0x0008) {	/* MIC VRA support */
		snd_ac97_determine_rates(ac97, AC97_PCM_MIC_ADC_RATE, &ac97->rates_mic_adc);
	} else {
		ac97->rates_mic_adc = SNDRV_PCM_RATE_48000;
	}
	if (ac97->ext_id & 0x0080) {	/* SDAC support */
		snd_ac97_determine_rates(ac97, AC97_PCM_SURR_DAC_RATE, &ac97->rates_surr_dac);
		ac97->scaps |= AC97_SCAP_SURROUND_DAC;
	}
	if (ac97->ext_id & 0x0100) {	/* LDAC support */
		snd_ac97_determine_rates(ac97, AC97_PCM_LFE_DAC_RATE, &ac97->rates_lfe_dac);
		ac97->scaps |= AC97_SCAP_CENTER_LFE_DAC;
	}
	if (ac97->init)
		ac97->init(ac97);
	snd_ac97_get_name(ac97, ac97->id, name);
	snd_ac97_get_name(NULL, ac97->id, name);  // ac97->id might be changed in the special setup code
	if (card->mixername[0] == '\0') {
		strcpy(card->mixername, name);
	} else {
		if (strlen(card->mixername) + 1 + strlen(name) + 1 <= sizeof(card->mixername)) {
			strcat(card->mixername, ",");
			strcat(card->mixername, name);
		}
	}
	if ((err = snd_component_add(card, "AC97")) < 0) {
		snd_ac97_free(ac97);
		return err;
	}
	if (snd_ac97_mixer_build(card, ac97) < 0) {
		snd_ac97_free(ac97);
		return -ENOMEM;
	}
	snd_ac97_proc_init(card, ac97);
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, ac97, &ops)) < 0) {
		snd_ac97_free(ac97);
		return err;
	}
	*rac97 = ac97;
	return 0;
}

/*

 */

static void snd_ac97_proc_read_main(ac97_t *ac97, snd_info_buffer_t * buffer, int subidx)
{
	char name[64];
	unsigned int id;
	unsigned short val, tmp, ext;
	static const char *spdif_slots[4] = { " SPDIF=3/4", " SPDIF=7/8", " SPDIF=6/9", " SPDIF=res" };
	static const char *spdif_rates[4] = { " Rate=44.1kHz", " Rate=res", " Rate=48kHz", " Rate=32kHz" };
	static const char *spdif_rates_cs4205[4] = { " Rate=48kHz", " Rate=44.1kHz", " Rate=res", " Rate=res" };

	id = snd_ac97_read(ac97, AC97_VENDOR_ID1) << 16;
	id |= snd_ac97_read(ac97, AC97_VENDOR_ID2);
	snd_ac97_get_name(NULL, id, name);
	snd_iprintf(buffer, "%d-%d/%d: %s\n\n", ac97->addr, ac97->num, subidx, name);
	val = snd_ac97_read(ac97, AC97_RESET);
	snd_iprintf(buffer, "Capabilities     :%s%s%s%s%s%s\n",
	    	    val & 0x0001 ? " -dedicated MIC PCM IN channel-" : "",
		    val & 0x0002 ? " -reserved1-" : "",
		    val & 0x0004 ? " -bass & treble-" : "",
		    val & 0x0008 ? " -simulated stereo-" : "",
		    val & 0x0010 ? " -headphone out-" : "",
		    val & 0x0020 ? " -loudness-" : "");
	tmp = ac97->caps & 0x00c0;
	snd_iprintf(buffer, "DAC resolution   : %s%s%s%s\n",
		    tmp == 0x0000 ? "16-bit" : "",
		    tmp == 0x0040 ? "18-bit" : "",
		    tmp == 0x0080 ? "20-bit" : "",
		    tmp == 0x00c0 ? "???" : "");
	tmp = ac97->caps & 0x0300;
	snd_iprintf(buffer, "ADC resolution   : %s%s%s%s\n",
		    tmp == 0x0000 ? "16-bit" : "",
		    tmp == 0x0100 ? "18-bit" : "",
		    tmp == 0x0200 ? "20-bit" : "",
		    tmp == 0x0300 ? "???" : "");
	snd_iprintf(buffer, "3D enhancement   : %s\n",
		snd_ac97_stereo_enhancements[(val >> 10) & 0x1f]);
	snd_iprintf(buffer, "\nCurrent setup\n");
	val = snd_ac97_read(ac97, AC97_MIC);
	snd_iprintf(buffer, "Mic gain         : %s [%s]\n", val & 0x0040 ? "+20dB" : "+0dB", ac97->regs[AC97_MIC] & 0x0040 ? "+20dB" : "+0dB");
	val = snd_ac97_read(ac97, AC97_GENERAL_PURPOSE);
	snd_iprintf(buffer, "POP path         : %s 3D\n"
		    "Sim. stereo      : %s\n"
		    "3D enhancement   : %s\n"
		    "Loudness         : %s\n"
		    "Mono output      : %s\n"
		    "Mic select       : %s\n"
		    "ADC/DAC loopback : %s\n",
		    val & 0x8000 ? "post" : "pre",
		    val & 0x4000 ? "on" : "off",
		    val & 0x2000 ? "on" : "off",
		    val & 0x1000 ? "on" : "off",
		    val & 0x0200 ? "Mic" : "MIX",
		    val & 0x0100 ? "Mic2" : "Mic1",
		    val & 0x0080 ? "on" : "off");
	ext = snd_ac97_read(ac97, AC97_EXTENDED_ID);

	if (ext == 0)
		return;
	snd_iprintf(buffer, "Extended ID      : codec=%i rev=%i%s%s%s%s DSA=%i%s%s%s%s\n",
			(ext >> 14) & 3,
			(ext >> 10) & 3,
			ext & 0x0200 ? " AMAP" : "",
			ext & 0x0100 ? " LDAC" : "",
			ext & 0x0080 ? " SDAC" : "",
			ext & 0x0040 ? " CDAC" : "",
			(ext >> 4) & 3,
			ext & 0x0008 ? " VRM" : "",
			ext & 0x0004 ? " SPDIF" : "",
			ext & 0x0002 ? " DRA" : "",
			ext & 0x0001 ? " VRA" : "");
	val = snd_ac97_read(ac97, AC97_EXTENDED_STATUS);
	snd_iprintf(buffer, "Extended status  :%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
			val & 0x4000 ? " PRL" : "",
			val & 0x2000 ? " PRK" : "",
			val & 0x1000 ? " PRJ" : "",
			val & 0x0800 ? " PRI" : "",
			val & 0x0400 ? " SPCV" : "",
			val & 0x0200 ? " MADC" : "",
			val & 0x0100 ? " LDAC" : "",
			val & 0x0080 ? " SDAC" : "",
			val & 0x0040 ? " CDAC" : "",
			ext & 0x0004 ? spdif_slots[(val & 0x0030) >> 4] : "",
			val & 0x0008 ? " VRM" : "",
			val & 0x0004 ? " SPDIF" : "",
			val & 0x0002 ? " DRA" : "",
			val & 0x0001 ? " VRA" : "");
	if (ext & 1) {	/* VRA */
		val = snd_ac97_read(ac97, AC97_PCM_FRONT_DAC_RATE);
		snd_iprintf(buffer, "PCM front DAC    : %iHz\n", val);
		if (ext & 0x0080) {
			val = snd_ac97_read(ac97, AC97_PCM_SURR_DAC_RATE);
			snd_iprintf(buffer, "PCM Surr DAC     : %iHz\n", val);
		}
		if (ext & 0x0100) {
			val = snd_ac97_read(ac97, AC97_PCM_LFE_DAC_RATE);
			snd_iprintf(buffer, "PCM LFE DAC      : %iHz\n", val);
		}
		val = snd_ac97_read(ac97, AC97_PCM_LR_ADC_RATE);
		snd_iprintf(buffer, "PCM ADC          : %iHz\n", val);
	}
	if (ext & 0x0008) {
		val = snd_ac97_read(ac97, AC97_PCM_MIC_ADC_RATE);
		snd_iprintf(buffer, "PCM MIC ADC      : %iHz\n", val);
	}
	if ((ext & 0x0004) || (ac97->flags & AC97_CS_SPDIF)) {
	        if (ac97->flags & AC97_CS_SPDIF)
			val = snd_ac97_read(ac97, AC97_CSR_SPDIF);
		else
			val = snd_ac97_read(ac97, AC97_SPDIF);

		snd_iprintf(buffer, "SPDIF Control    :%s%s%s%s Category=0x%x Generation=%i%s%s%s\n",
			val & 0x0001 ? " PRO" : " Consumer",
			val & 0x0002 ? " Non-audio" : " PCM",
			val & 0x0004 ? " Copyright" : "",
			val & 0x0008 ? " Preemph50/15" : "",
			(val & 0x07f0) >> 4,
			(val & 0x0800) >> 11,
			(ac97->flags & AC97_CS_SPDIF) ?
			    spdif_rates_cs4205[(val & 0x3000) >> 12] :
			    spdif_rates[(val & 0x3000) >> 12],
			(ac97->flags & AC97_CS_SPDIF) ?
			    (val & 0x4000 ? " Validity" : "") :
			    (val & 0x4000 ? " DRS" : ""),
			(ac97->flags & AC97_CS_SPDIF) ?
			    (val & 0x8000 ? " Enabled" : "") :
			    (val & 0x8000 ? " Validity" : ""));
	}
}

static void snd_ac97_proc_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	ac97_t *ac97 = snd_magic_cast(ac97_t, entry->private_data, return);
	
	if ((ac97->id & 0xffffff40) == AC97_ID_AD1881) {	// Analog Devices AD1881/85/86
		int idx;
		down(&ac97->spec.ad18xx.mutex);
		for (idx = 0; idx < 3; idx++)
			if (ac97->spec.ad18xx.id[idx]) {
				/* select single codec */
				snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, ac97->spec.ad18xx.unchained[idx] | ac97->spec.ad18xx.chained[idx]);
				snd_ac97_proc_read_main(ac97, buffer, idx);
				snd_iprintf(buffer, "\n\n");
			}
		/* select all codecs */
		snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, 0x7000);
		up(&ac97->spec.ad18xx.mutex);
		
		snd_iprintf(buffer, "\nAD18XX configuration\n");
		snd_iprintf(buffer, "Unchained        : 0x%04x,0x%04x,0x%04x\n",
			ac97->spec.ad18xx.unchained[0],
			ac97->spec.ad18xx.unchained[1],
			ac97->spec.ad18xx.unchained[2]);
		snd_iprintf(buffer, "Chained          : 0x%04x,0x%04x,0x%04x\n",
			ac97->spec.ad18xx.chained[0],
			ac97->spec.ad18xx.chained[1],
			ac97->spec.ad18xx.chained[2]);
	} else {
		snd_ac97_proc_read_main(ac97, buffer, 0);
	}
}

static void snd_ac97_proc_regs_read_main(ac97_t *ac97, snd_info_buffer_t * buffer, int subidx)
{
	int reg, val;

	for (reg = 0; reg < 0x80; reg += 2) {
		val = snd_ac97_read(ac97, reg);
		snd_iprintf(buffer, "%i:%02x = %04x\n", subidx, reg, val);
	}
}

static void snd_ac97_proc_regs_read(snd_info_entry_t *entry, 
				    snd_info_buffer_t * buffer)
{
	ac97_t *ac97 = snd_magic_cast(ac97_t, entry->private_data, return);

	if ((ac97->id & 0xffffff40) == AC97_ID_AD1881) {	// Analog Devices AD1881/85/86

		int idx;
		down(&ac97->spec.ad18xx.mutex);
		for (idx = 0; idx < 3; idx++)
			if (ac97->spec.ad18xx.id[idx]) {
				/* select single codec */
				snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, ac97->spec.ad18xx.unchained[idx] | ac97->spec.ad18xx.chained[idx]);
				snd_ac97_proc_regs_read_main(ac97, buffer, idx);
			}
		/* select all codecs */
		snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, 0x7000);
		up(&ac97->spec.ad18xx.mutex);
	} else {
		snd_ac97_proc_regs_read_main(ac97, buffer, 0);
	}	
}

static void snd_ac97_proc_init(snd_card_t * card, ac97_t * ac97)
{
	snd_info_entry_t *entry;
	char name[32];

	if (ac97->num)
		sprintf(name, "ac97#%d-%d", ac97->addr, ac97->num);
	else
		sprintf(name, "ac97#%d", ac97->addr);
	if ((entry = snd_info_create_card_entry(card, name, card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = ac97;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 512;
		entry->c.text.read = snd_ac97_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ac97->proc_entry = entry;
	if (ac97->num)
		sprintf(name, "ac97#%d-%dregs", ac97->addr, ac97->num);
	else
		sprintf(name, "ac97#%dregs", ac97->addr);
	if ((entry = snd_info_create_card_entry(card, name, card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = ac97;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 1024;
		entry->c.text.read = snd_ac97_proc_regs_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ac97->proc_regs_entry = entry;
}

static void snd_ac97_proc_done(ac97_t * ac97)
{
	if (ac97->proc_regs_entry) {
		snd_info_unregister(ac97->proc_regs_entry);
		ac97->proc_regs_entry = NULL;
	}
	if (ac97->proc_entry) {
		snd_info_unregister(ac97->proc_entry);
		ac97->proc_entry = NULL;
	}
}

/*
 *  Chip specific initialization
 */

static int patch_wolfson00(ac97_t * ac97)
{
	/* This sequence is suspect because it was designed for
	   the WM9704, and is known to fail when applied to the
	   WM9707.  If you're having trouble initializing a
	   WM9700, this is the place to start looking.
	   Randolph Bentson <bentson@holmsjoen.com> */

	// WM9701A
	snd_ac97_write_cache(ac97, 0x72, 0x0808);
	snd_ac97_write_cache(ac97, 0x74, 0x0808);

	// patch for DVD noise
	snd_ac97_write_cache(ac97, 0x5a, 0x0200);

	// init vol
	snd_ac97_write_cache(ac97, 0x70, 0x0808);

	snd_ac97_write_cache(ac97, AC97_SURROUND_MASTER, 0x0000);
	return 0;
}

static int patch_wolfson03(ac97_t * ac97)
{
	/* This is known to work for the ViewSonic ViewPad 1000
	   Randolph Bentson <bentson@holmsjoen.com> */

	// WM9703/9707
	snd_ac97_write_cache(ac97, 0x72, 0x0808);
	snd_ac97_write_cache(ac97, 0x20, 0x8000);
	return 0;
}

static int patch_wolfson04(ac97_t * ac97)
{
	// WM9704
	snd_ac97_write_cache(ac97, 0x72, 0x0808);
	snd_ac97_write_cache(ac97, 0x74, 0x0808);

	// patch for DVD noise
	snd_ac97_write_cache(ac97, 0x5a, 0x0200);

	// init vol
	snd_ac97_write_cache(ac97, 0x70, 0x0808);

	snd_ac97_write_cache(ac97, AC97_SURROUND_MASTER, 0x0000);
	return 0;
}

static int patch_tritech_tr28028(ac97_t * ac97)
{
	snd_ac97_write_cache(ac97, 0x26, 0x0300);
	snd_ac97_write_cache(ac97, 0x26, 0x0000);
	snd_ac97_write_cache(ac97, AC97_SURROUND_MASTER, 0x0000);
	snd_ac97_write_cache(ac97, AC97_SPDIF, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9708(ac97_t * ac97)
{
	unsigned int codec72, codec6c;

	codec72 = snd_ac97_read(ac97, AC97_SIGMATEL_BIAS2) & 0x8000;
	codec6c = snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG);

	if ((codec72==0) && (codec6c==0)) {
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x1000);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0007);
	} else if ((codec72==0x8000) && (codec6c==0)) {
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x1001);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_DAC2INVERT, 0x0008);
	} else if ((codec72==0x8000) && (codec6c==0x0080)) {
		/* nothing */
	}
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9721(ac97_t * ac97)
{
	if (snd_ac97_read(ac97, AC97_SIGMATEL_ANALOG) == 0) {
		// patch for SigmaTel
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x4000);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
		snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	}
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9744(ac97_t * ac97)
{
	// patch for SigmaTel
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x0000);	/* is this correct? --jk */
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int patch_sigmatel_stac9756(ac97_t * ac97)
{
	// patch for SigmaTel
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_CIC2, 0x0000);	/* is this correct? --jk */
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS1, 0xabba);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_BIAS2, 0x0002);
	snd_ac97_write_cache(ac97, AC97_SIGMATEL_MULTICHN, 0x0000);
	return 0;
}

static int patch_cirrus_spdif(ac97_t * ac97)
{
	/* Basically, the cs4201/cs4205/cs4297a has non-standard sp/dif registers.
	   WHY CAN'T ANYONE FOLLOW THE BLOODY SPEC?  *sigh*
	   - sp/dif EA ID is not set, but sp/dif is always present.
	   - enable/disable is spdif register bit 15.
	   - sp/dif control register is 0x68.  differs from AC97:
	   - valid is bit 14 (vs 15)
	   - no DRS
	   - only 44.1/48k [00 = 48, 01=44,1] (AC97 is 00=44.1, 10=48)
	   - sp/dif ssource select is in 0x5e bits 0,1.
	*/

	ac97->flags |= AC97_CS_SPDIF; 
        ac97->ext_id |= AC97_EA_SPDIF;	/* force the detection of spdif */
	snd_ac97_write_cache(ac97, AC97_CSR_ACMODE, 0x0080);
	return 0;
}

static int patch_cirrus_cs4299(ac97_t * ac97)
{
	/* force the detection of PC Beep */
	ac97->flags |= AC97_HAS_PC_BEEP;
	
	return patch_cirrus_spdif(ac97);
}

static int patch_ad1819(ac97_t * ac97)
{
	// patch for Analog Devices
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, 0x7000); /* select all codecs */
	return 0;
}

static unsigned short patch_ad1881_unchained(ac97_t * ac97, int idx, unsigned short mask)
{
	unsigned short val;

	// test for unchained codec
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, mask);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0000);	/* ID0C, ID1C, SDIE = off */
	val = snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if ((val & 0xff40) != 0x5340)
		return 0;
	ac97->spec.ad18xx.unchained[idx] = mask;
	ac97->spec.ad18xx.id[idx] = val;
	return mask;
}

static int patch_ad1881_chained1(ac97_t * ac97, int idx, unsigned short codec_bits)
{
	static int cfg_bits[3] = { 1<<12, 1<<14, 1<<13 };
	unsigned short val;
	
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, cfg_bits[idx]);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0004);	// SDIE
	val = snd_ac97_read(ac97, AC97_VENDOR_ID2);
	if ((val & 0xff40) != 0x5340)
		return 0;
	if (codec_bits)
		snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, codec_bits);
	ac97->spec.ad18xx.chained[idx] = cfg_bits[idx];
	ac97->spec.ad18xx.id[idx] = val;
	return 1;
}

static void patch_ad1881_chained(ac97_t * ac97, int unchained_idx, int cidx1, int cidx2)
{
	// already detected?
	if (ac97->spec.ad18xx.unchained[cidx1] || ac97->spec.ad18xx.chained[cidx1])
		cidx1 = -1;
	if (ac97->spec.ad18xx.unchained[cidx2] || ac97->spec.ad18xx.chained[cidx2])
		cidx2 = -1;
	if (cidx1 < 0 && cidx2 < 0)
		return;
	// test for chained codecs
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, ac97->spec.ad18xx.unchained[unchained_idx]);
	snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0002);		// ID1C
	if (cidx1 >= 0) {
		if (patch_ad1881_chained1(ac97, cidx1, 0x0006))		// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx2, 0);
		else if (patch_ad1881_chained1(ac97, cidx2, 0x0006))	// SDIE | ID1C
			patch_ad1881_chained1(ac97, cidx1, 0);
	} else if (cidx2 >= 0) {
		patch_ad1881_chained1(ac97, cidx2, 0);
	}
}

static int patch_ad1881(ac97_t * ac97)
{
	static const char cfg_idxs[3][2] = {
		{2, 1},
		{0, 2},
		{0, 1}
	};
	
	// patch for Analog Devices
	unsigned short codecs[3];
	int idx, num;

	init_MUTEX(&ac97->spec.ad18xx.mutex);

	codecs[0] = patch_ad1881_unchained(ac97, 0, (1<<12));
	codecs[1] = patch_ad1881_unchained(ac97, 1, (1<<14));
	codecs[2] = patch_ad1881_unchained(ac97, 2, (1<<13));

	snd_runtime_check(codecs[0] | codecs[1] | codecs[2], goto __end);

	for (idx = 0; idx < 3; idx++)
		if (ac97->spec.ad18xx.unchained[idx])
			patch_ad1881_chained(ac97, idx, cfg_idxs[idx][0], cfg_idxs[idx][1]);

	if (ac97->spec.ad18xx.id[1]) {
		ac97->flags |= AC97_AD_MULTI;
		ac97->scaps |= AC97_SCAP_SURROUND_DAC;
	}
	if (ac97->spec.ad18xx.id[2]) {
		ac97->flags |= AC97_AD_MULTI;
		ac97->scaps |= AC97_SCAP_CENTER_LFE_DAC;
	}

      __end:
	/* select all codecs */
	snd_ac97_write_cache(ac97, AC97_AD_SERIAL_CFG, 0x7000);
	/* check if only one codec is present */
	for (idx = num = 0; idx < 3; idx++)
		if (ac97->spec.ad18xx.id[idx])
			num++;
	if (num == 1) {
		/* ok, deselect all ID bits */
		snd_ac97_write_cache(ac97, AC97_AD_CODEC_CFG, 0x0000);
	}
	/* required for AD1886/AD1885 combination */
	ac97->ext_id = snd_ac97_read(ac97, AC97_EXTENDED_ID);
	if (ac97->spec.ad18xx.id[0]) {
		ac97->id &= 0xffff0000;
		ac97->id |= ac97->spec.ad18xx.id[0];
	}
	return 0;
}

static int patch_ad1886(ac97_t * ac97)
{
	patch_ad1881(ac97);
	/* Presario700 workaround */
	/* for Jack Sense/SPDIF Register misetting causing */
	snd_ac97_write_cache(ac97, AC97_AD_JACK_SPDIF, 0x0010);
	return 0;
}

/*
 *  PCM support
 */

int snd_ac97_set_rate(ac97_t *ac97, int reg, unsigned short rate)
{
	unsigned short mask;
	unsigned int tmp;
	signed long end_time;
	
	switch (reg) {
	case AC97_PCM_MIC_ADC_RATE:
		mask = 0x0000;
		if ((ac97->regs[AC97_EXTENDED_STATUS] & 0x0008) == 0)	/* MIC VRA */
			if (rate != 48000)
				return -EINVAL;
		break;
	case AC97_PCM_FRONT_DAC_RATE:
		mask = 0x0200;
		if ((ac97->regs[AC97_EXTENDED_STATUS] & 0x0001) == 0)	/* VRA */
			if (rate != 48000)
				return -EINVAL;
		break;
	case AC97_PCM_LR_ADC_RATE:
		mask = 0x0100;
		if ((ac97->regs[AC97_EXTENDED_STATUS] & 0x0001) == 0)	/* VRA */
			if (rate != 48000)
				return -EINVAL;
		break;
	default: return -EINVAL;
	}
	tmp = ((unsigned int)rate * ac97->clock) / 48000;
	if (tmp > 65535)
		return -EINVAL;
	snd_ac97_update_bits(ac97, AC97_POWERDOWN, mask, mask);
	end_time = jiffies + (HZ / 50);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (end_time - (signed long)jiffies >= 0);
	snd_ac97_update(ac97, reg, tmp & 0xffff);
	udelay(10);
	// XXXX update spdif rate here too?
	snd_ac97_update_bits(ac97, AC97_POWERDOWN, mask, 0);
	end_time = jiffies + (HZ / 50);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (end_time - (signed long)jiffies >= 0);
	end_time = jiffies + (HZ / 10);
	do {
		if ((snd_ac97_read(ac97, AC97_POWERDOWN) & 0x0003) == 0x0003)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (end_time - (signed long)jiffies >= 0);
	return 0;
}


#ifdef CONFIG_PM
/*
 * general suspend procedure
 */
void snd_ac97_suspend(ac97_t *ac97)
{
	unsigned short power = (ac97->regs[AC97_POWERDOWN] ^ 0x8000) & ~0x8000;	/* invert EAPD */

	power |= 0x4000;	/* Headphone amplifier powerdown */
	power |= 0x0300;	/* ADC & DAC powerdown */
	snd_ac97_write(ac97, AC97_POWERDOWN, power);
	udelay(100);
	power |= 0x0400;	/* Analog Mixer powerdown (Vref on) */
	snd_ac97_write(ac97, AC97_POWERDOWN, power);
	udelay(100);
	power |= 0x3800;	/* AC-link powerdown, internal Clk disable */
	snd_ac97_write(ac97, AC97_POWERDOWN, power);
}

/*
 * general resume procedure
 */
void snd_ac97_resume(ac97_t *ac97)
{
	int i;

	snd_ac97_write(ac97, AC97_POWERDOWN, 0);
	snd_ac97_write(ac97, AC97_RESET, 0);
	udelay(100);
	snd_ac97_write(ac97, AC97_POWERDOWN, 0);
	snd_ac97_write(ac97, AC97_GENERAL_PURPOSE, 0);

	snd_ac97_write(ac97, AC97_POWERDOWN, ac97->regs[AC97_POWERDOWN]);
	snd_ac97_write(ac97, AC97_MASTER, 0x8000);
	for (i = 0; i < 10; i++) {
		if (snd_ac97_read(ac97, AC97_MASTER) == 0x8000)
			break;
		mdelay(1);
	}

	if (ac97->init)
		ac97->init(ac97);

	/* restore ac97 status */
	for (i = 2; i < 0x7c ; i += 2) {
		if (i == AC97_POWERDOWN || i == AC97_EXTENDED_ID)
			continue;
		/* restore only accessible registers
		 * some chip (e.g. nm256) may hang up when unsupported registers
		 * are accessed..!
		 */
		if (test_bit(i, &ac97->reg_accessed))
			snd_ac97_write(ac97, i, ac97->regs[i]);
	}
}
#endif


/*
 *  Exported symbols
 */

EXPORT_SYMBOL(snd_ac97_write);
EXPORT_SYMBOL(snd_ac97_read);
EXPORT_SYMBOL(snd_ac97_write_cache);
EXPORT_SYMBOL(snd_ac97_update);
EXPORT_SYMBOL(snd_ac97_update_bits);
EXPORT_SYMBOL(snd_ac97_mixer);
EXPORT_SYMBOL(snd_ac97_set_rate);
#ifdef CONFIG_PM
EXPORT_SYMBOL(snd_ac97_resume);
#endif

/*
 *  INIT part
 */

static int __init alsa_ac97_init(void)
{
	return 0;
}

static void __exit alsa_ac97_exit(void)
{
}

module_init(alsa_ac97_init)
module_exit(alsa_ac97_exit)
