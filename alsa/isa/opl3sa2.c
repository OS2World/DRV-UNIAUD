/*
 *  Driver for Yamaha OPL3-SA[2,3] soundcards
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
#include <sound/cs4231.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("Yamaha OPL3SA2+");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Yamaha,YMF719E-S},"
		"{Genius,Sound Maker 3DX},"
		"{Yamaha,OPL3SA3},"
		"{Intel,AL440LX sound},"
	        "{NeoMagic,MagicWave 3DX}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
#ifdef __ISAPNP__
#ifdef TARGET_OS2
static int snd_isapnp[SNDRV_CARDS] = {1,1,1,1,1,1,1,1};
#else
static int snd_isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
#endif
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0xf86,0x370,0x100 */
static long snd_sb_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260 */
static long snd_wss_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;/* 0x530,0xe80,0xf40,0x604 */
static long snd_fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x388 */
static long snd_midi_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;/* 0x330,0x300 */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 0,1,3,5,9,11,12,15 */
static int snd_dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int snd_dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
#ifdef TARGET_OS2
static int snd_opl3sa3_ymode[SNDRV_CARDS] = { 0,0,0,0,0,0,0,0 };   /* 0,1,2,3 */ /*SL Added*/
#else
static int snd_opl3sa3_ymode[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = 0 };   /* 0,1,2,3 */ /*SL Added*/
#endif

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for OPL3-SA soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for OPL3-SA soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable OPL3-SA soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_isapnp, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_isapnp, "ISA PnP detection for specified soundcard.");
MODULE_PARM_SYNTAX(snd_isapnp, SNDRV_ISAPNP_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for OPL3-SA driver.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_ENABLED ",allows:{{0xf86},{0x370},{0x100}},dialog:list");
MODULE_PARM(snd_sb_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_sb_port, "SB port # for OPL3-SA driver.");
MODULE_PARM_SYNTAX(snd_sb_port, SNDRV_ENABLED ",allows:{{0x220},{0x240},{0x260}},dialog:list");
MODULE_PARM(snd_wss_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_wss_port, "WSS port # for OPL3-SA driver.");
MODULE_PARM_SYNTAX(snd_wss_port, SNDRV_ENABLED ",allows:{{0x530},{0xe80},{0xf40},{0x604}},dialog:list");
MODULE_PARM(snd_fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_fm_port, "FM port # for OPL3-SA driver.");
MODULE_PARM_SYNTAX(snd_fm_port, SNDRV_ENABLED ",allows:{{0x388}},dialog:list");
MODULE_PARM(snd_midi_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_midi_port, "MIDI port # for OPL3-SA driver.");
MODULE_PARM_SYNTAX(snd_midi_port, SNDRV_ENABLED ",allows:{{0x330},{0x300}},dialog:list");
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for OPL3-SA driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_ENABLED ",allows:{{0},{1},{3},{5},{9},{11},{12},{15}},dialog:list");
MODULE_PARM(snd_dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma1, "DMA1 # for OPL3-SA driver.");
MODULE_PARM_SYNTAX(snd_dma1, SNDRV_ENABLED ",allows:{{1},{3},{5},{6},{7}},dialog:list");
MODULE_PARM(snd_dma2, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma2, "DMA2 # for OPL3-SA driver.");
MODULE_PARM_SYNTAX(snd_dma2, SNDRV_ENABLED ",allows:{{1},{3},{5},{6},{7}},dialog:list");
MODULE_PARM(snd_opl3sa3_ymode, "1-" __MODULE_STRING(SNDRV_CARDS) "i"); /* SL Added */
MODULE_PARM_DESC(snd_opl3sa3_ymode, "Speaker size selection for 3D Enhancement mode: Desktop/Large Notebook/Small Notebook/HiFi.");
MODULE_PARM_SYNTAX(snd_opl3sa3_ymode, SNDRV_ENABLED ",allows:{{0,3}},dialog:list");  /* SL Added */

struct snd_opl3sa {
	snd_card_t *card;
	int version;		/* 2 or 3 */
	unsigned long port;	/* control port */
	struct resource *res_port; /* control port resource */
	int irq;
	int single_dma;
	spinlock_t reg_lock;
	snd_hwdep_t *synth;
	snd_rawmidi_t *rmidi;
	cs4231_t *cs4231;
#ifdef __ISAPNP__
	struct isapnp_dev *dev;
#endif
	unsigned char ctlregs[0x20];
	int ymode;		/* SL added */
	snd_kcontrol_t *master_switch;
	snd_kcontrol_t *master_volume;
};

static snd_card_t *snd_opl3sa_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef __ISAPNP__

static struct isapnp_card *snd_opl3sa2_isapnp_cards[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;
static const struct isapnp_card_id *snd_opl3sa2_isapnp_id[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;

#ifdef TARGET_OS2
#define ISAPNP_OPL3SA2(_va, _vb, _vc, _device, _function) \
        { \
                0, ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
                { ISAPNP_DEVICE_ID(_va, _vb, _vc, _function), } \
        }
#else
#define ISAPNP_OPL3SA2(_va, _vb, _vc, _device, _function) \
        { \
                ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
                devs : { ISAPNP_DEVICE_ID(_va, _vb, _vc, _function), } \
        }
#endif

static struct isapnp_card_id snd_opl3sa2_pnpids[] __devinitdata = {
	/* Yamaha YMF719E-S (Genius Sound Maker 3DX) */
	ISAPNP_OPL3SA2('Y','M','H',0x0020,0x0021),
	/* Yamaha OPL3-SA3 (integrated on Intel's Pentium II AL440LX motherboard) */
	ISAPNP_OPL3SA2('Y','M','H',0x0030,0x0021),
	/* ??? */
	ISAPNP_OPL3SA2('Y','M','H',0x0800,0x0021),
	/* NeoMagic MagicWave 3DX */
	ISAPNP_OPL3SA2('N','M','X',0x2200,0x2210),
	/* --- */
	{ ISAPNP_CARD_END, }	/* end */
};

ISAPNP_CARD_TABLE(snd_opl3sa2_pnpids);

#endif /* __ISAPNP__ */

static unsigned char snd_opl3sa_read(unsigned long port, unsigned char reg)
{
	unsigned long flags;
	unsigned char result;

	save_flags(flags);
	cli();
#if 0
	outb(0x1d, port);	/* password */
	printk("read [0x%lx] = 0x%x\n", port, inb(port));
#endif
	outb(reg, port);	/* register */
	result = inb(port + 1);
	restore_flags(flags);
#if 0
	printk("read [0x%lx] = 0x%x [0x%x]\n", port, result, inb(port));
#endif
	return result;
}

static void snd_opl3sa_write(unsigned long port,
			     unsigned char reg, unsigned char value)
{
	unsigned long flags;

	save_flags(flags);
	cli();
#if 0
	outb(0x1d, port);	/* password */
#endif
	outb(reg, port);	/* register */
	outb(value, port + 1);
	restore_flags(flags);
}

static int __init snd_opl3sa_detect(struct snd_opl3sa *oplcard)
{
	snd_card_t *card;
	unsigned long port;
	unsigned char tmp, tmp1;
	char str[2];

	card = oplcard->card;
	port = oplcard->port;
	if ((oplcard->res_port = request_region(port, 2, "OPL3-SA control")) == NULL)
		return -EBUSY;
	// snd_printk("REG 0A = 0x%x\n", snd_opl3sa_read(port, 0x0a));
	oplcard->version = 0;
	tmp = snd_opl3sa_read(port, 0x0a);
	if (tmp == 0xff) {
		snd_printd("OPL3-SA [0x%lx] detect = 0x%x\n", port, tmp);
		return -ENODEV;
	}
	switch (tmp & 0x07) {
	case 0x01:
		oplcard->version = 2;
		break;
	default:
		oplcard->version = 3;
		/* 0x02 - standard */
		/* 0x03 - YM715B */
		/* 0x04 - YM719 - OPL-SA4? */
		/* 0x05 - OPL3-SA3 - Libretto 100 */
		break;
	}
	str[0] = oplcard->version + '0';
	str[1] = 0;
	strcat(card->shortname, str);
	snd_opl3sa_write(port, 0x0a, tmp ^ 7);
	if ((tmp1 = snd_opl3sa_read(port, 0x0a)) != tmp) {
		snd_printd("OPL3-SA [0x%lx] detect (1) = 0x%x (0x%x)\n", port, tmp, tmp1);
		return -ENODEV;
	}
	/* try if the MIC register is accesible */
	tmp = snd_opl3sa_read(port, 0x09);
	snd_opl3sa_write(port, 0x09, 0x8a);
	if (((tmp1 = snd_opl3sa_read(port, 0x09)) & 0x9f) != 0x8a) {
		snd_printd("OPL3-SA [0x%lx] detect (2) = 0x%x (0x%x)\n", port, tmp, tmp1);
		return -ENODEV;
	}
	snd_opl3sa_write(port, 0x09, 0x9f);
	/* initialization */
	snd_opl3sa_write(port, 0x01, 0x00);	/* Power Management - default */
	if (oplcard->version > 2) {   /* SL Added */
		snd_opl3sa_write(port, 0x02, (oplcard->ymode << 4)); /* SL Modified - System Control - ymode is bits 4&5 (of 0 to 7) on all but opl3sa2 versions */
	} else { /* SL Added */
		snd_opl3sa_write(port, 0x02, 0x00);	/* SL Modified - System Control - default for opl3sa2 versions */
	} /* SL Added */
	snd_opl3sa_write(port, 0x03, 0x0d);	/* Interrupt Channel Configuration - IRQ A = OPL3 + MPU + WSS */
	if (oplcard->single_dma) {
		snd_opl3sa_write(port, 0x06, 0x03);	/* DMA Configuration - DMA A = WSS-R + WSS-P */
	} else {
		snd_opl3sa_write(port, 0x06, 0x21);	/* DMA Configuration - DMA B = WSS-R, DMA A = WSS-P */
	}
	snd_opl3sa_write(port, 0x0a, 0x80 | (tmp & 7));	/* Miscellaneous - default */
	if (oplcard->version > 2) {
		snd_opl3sa_write(port, 0x12, 0x00);	/* Digital Block Partial Power Down - default */
		snd_opl3sa_write(port, 0x13, 0x00);	/* Analog Block Partial Power Down - default */
	}
	return 0;
}

static void snd_opl3sa_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned short status;
	struct snd_opl3sa *oplcard = (struct snd_opl3sa *) dev_id;

	if (oplcard == NULL || oplcard->card == NULL)
		return;

	spin_lock(&oplcard->reg_lock);
	outb(0x04, oplcard->port);	/* register - Interrupt IRQ-A status */
	status = inb(oplcard->port + 1);
	spin_unlock(&oplcard->reg_lock);

	if (status & 0x20)
		snd_opl3_interrupt(oplcard->synth);

	if ((status & 0x10) && oplcard->rmidi != NULL)
		snd_mpu401_uart_interrupt(irq, oplcard->rmidi->private_data, regs);

	if (status & 0x07)	/* TI,CI,PI */
		snd_cs4231_interrupt(irq, oplcard->cs4231, regs);

	if (status & 0x40) {
		/* reading from Master Lch register at 0x07 clears this bit */
		snd_opl3sa_read(oplcard->port, 0x08);
		snd_opl3sa_read(oplcard->port, 0x07);
		snd_ctl_notify(oplcard->card, SNDRV_CTL_EVENT_MASK_VALUE, &oplcard->master_switch->id);
		snd_ctl_notify(oplcard->card, SNDRV_CTL_EVENT_MASK_VALUE, &oplcard->master_volume->id);
	}
}

#ifdef TARGET_OS2
#define OPL3SA_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, xindex, \
  0, snd_opl3sa_info_single, \
  snd_opl3sa_get_single, snd_opl3sa_put_single, \
  reg | (shift << 8) | (mask << 16) | (invert << 24) }

#else
#define OPL3SA_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_opl3sa_info_single, \
  get: snd_opl3sa_get_single, put: snd_opl3sa_put_single, \
  private_value: reg | (shift << 8) | (mask << 16) | (invert << 24) }
#endif

static int snd_opl3sa_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

int snd_opl3sa_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	struct snd_opl3sa *oplcard = (struct snd_opl3sa *)_snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;

	spin_lock_irqsave(&oplcard->reg_lock, flags);
	ucontrol->value.integer.value[0] = (oplcard->ctlregs[reg] >> shift) & mask;
	spin_unlock_irqrestore(&oplcard->reg_lock, flags);
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

int snd_opl3sa_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	struct snd_opl3sa *oplcard = (struct snd_opl3sa *)_snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	int change;
	unsigned short val, oval;
	
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	val <<= shift;
	spin_lock_irqsave(&oplcard->reg_lock, flags);
	oval = oplcard->ctlregs[reg];
	val = (oval & ~(mask << shift)) | val;
	change = val != oval;
	snd_opl3sa_write(oplcard->port, reg, oplcard->ctlregs[reg] = val);
	spin_unlock_irqrestore(&oplcard->reg_lock, flags);
	return change;
}

#ifdef TARGET_OS2
#define OPL3SA_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, xindex, \
  0, snd_opl3sa_info_double, \
  snd_opl3sa_get_double, snd_opl3sa_put_double, \
  left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }

#else
#define OPL3SA_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, index: xindex, \
  info: snd_opl3sa_info_double, \
  get: snd_opl3sa_get_double, put: snd_opl3sa_put_double, \
  private_value: left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }
#endif

int snd_opl3sa_info_double(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

int snd_opl3sa_get_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	struct snd_opl3sa *oplcard = (struct snd_opl3sa *)_snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	
	spin_lock_irqsave(&oplcard->reg_lock, flags);
	ucontrol->value.integer.value[0] = (oplcard->ctlregs[left_reg] >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (oplcard->ctlregs[right_reg] >> shift_right) & mask;
	spin_unlock_irqrestore(&oplcard->reg_lock, flags);
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

int snd_opl3sa_put_double(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	struct snd_opl3sa *oplcard = (struct snd_opl3sa *)_snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	unsigned short val1, val2, oval1, oval2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;
	spin_lock_irqsave(&oplcard->reg_lock, flags);
	if (left_reg != right_reg) {
		oval1 = oplcard->ctlregs[left_reg];
		oval2 = oplcard->ctlregs[right_reg];
		val1 = (oval1 & ~(mask << shift_left)) | val1;
		val2 = (oval2 & ~(mask << shift_right)) | val2;
		change = val1 != oval1 || val2 != oval2;
		snd_opl3sa_write(oplcard->port, left_reg, oplcard->ctlregs[left_reg] = val1);
		snd_opl3sa_write(oplcard->port, right_reg, oplcard->ctlregs[right_reg] = val2);
	} else {
		oval1 = oplcard->ctlregs[left_reg];
		val1 = (oval1 & ~((mask << shift_left) | (mask << shift_right))) | val1 | val2;
		change = val1 != oval1;
		snd_opl3sa_write(oplcard->port, left_reg, oplcard->ctlregs[left_reg] = val1);
	}
	spin_unlock_irqrestore(&oplcard->reg_lock, flags);
	return change;
}

#define OPL3SA_CONTROLS (sizeof(snd_opl3sa_controls)/sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_opl3sa_controls[] = {
OPL3SA_DOUBLE("Master Playback Switch", 0, 0x07, 0x08, 7, 7, 1, 1),
OPL3SA_DOUBLE("Master Playback Volume", 0, 0x07, 0x08, 0, 0, 15, 1),
OPL3SA_SINGLE("Mic Playback Switch", 0, 0x09, 7, 1, 1),
OPL3SA_SINGLE("Mic Playback Volume", 0, 0x09, 0, 31, 1)
};

#define OPL3SA_TONE_CONTROLS (sizeof(snd_opl3sa_tone_controls)/sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_opl3sa_tone_controls[] = {
OPL3SA_DOUBLE("3D Control - Wide", 0, 0x14, 0x14, 4, 0, 7, 0),
OPL3SA_DOUBLE("Tone Control - Bass", 0, 0x15, 0x15, 4, 0, 7, 0),
OPL3SA_DOUBLE("Tone Control - Treble", 0, 0x16, 0x16, 4, 0, 7, 0)
};

static void snd_opl3sa_master_free(snd_kcontrol_t *kcontrol)
{
	struct snd_opl3sa *oplcard = (struct snd_opl3sa *)_snd_kcontrol_chip(kcontrol);
	
	oplcard->master_switch = NULL;
	oplcard->master_volume = NULL;
}

static int __init snd_opl3sa_mixer(struct snd_opl3sa *oplcard)
{
	snd_card_t *card = oplcard->card;
	snd_ctl_elem_id_t id1, id2;
	snd_kcontrol_t *kctl;
	int idx, err;

	memset(&id1, 0, sizeof(id1));
	memset(&id2, 0, sizeof(id2));
	id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	/* reassign AUX0 to CD */
        strcpy(id1.name, "Aux Playback Switch");
        strcpy(id2.name, "CD Playback Switch");
        if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
                return err;
        strcpy(id1.name, "Aux Playback Volume");
        strcpy(id2.name, "CD Playback Volume");
        if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
                return err;
	/* reassign AUX1 to FM */
        strcpy(id1.name, "Aux Playback Switch"); id1.index = 1;
        strcpy(id2.name, "FM Playback Switch");
        if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
                return err;
        strcpy(id1.name, "Aux Playback Volume");
        strcpy(id2.name, "FM Playback Volume");
        if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
                return err;
	/* add OPL3SA2 controls */
	for (idx = 0; idx < OPL3SA_CONTROLS; idx++) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_opl3sa_controls[idx], oplcard))) < 0)
			return err;
		switch (idx) {
		case 0: oplcard->master_switch = kctl; kctl->private_free = snd_opl3sa_master_free; break;
		case 1: oplcard->master_volume = kctl; kctl->private_free = snd_opl3sa_master_free; break;
		}
	}
	if (oplcard->version > 2) {
		for (idx = 0; idx < OPL3SA_TONE_CONTROLS; idx++)
			if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_opl3sa_tone_controls[idx], oplcard))) < 0)
				return err;
	}
	return 0;
}

#ifdef __ISAPNP__
static int __init snd_opl3sa_isapnp(int dev, struct snd_opl3sa *oplcard)
{
        const struct isapnp_card_id *id = snd_opl3sa2_isapnp_id[dev];
        struct isapnp_card *card = snd_opl3sa2_isapnp_cards[dev];
	struct isapnp_dev *pdev;

	oplcard->dev = isapnp_find_dev(card, id->devs[0].vendor, id->devs[0].function, NULL);
	if (oplcard->dev->active) {
		oplcard->dev = NULL;
		return -EBUSY;
	}
	/* PnP initialization */
	pdev = oplcard->dev;
	if (pdev->prepare(pdev)<0)
		return -EAGAIN;
	if (snd_sb_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], snd_sb_port[dev], 16);
	if (snd_wss_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[1], snd_wss_port[dev], 8);
	if (snd_fm_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[2], snd_fm_port[dev], 4);
	if (snd_midi_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[3], snd_midi_port[dev], 2);
	if (snd_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[4], snd_port[dev], 2);
	if (snd_dma1[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[0], snd_dma1[dev], 1);
	if (snd_dma2[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[1], snd_dma2[dev], 1);
	if (snd_irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], snd_irq[dev], 1);
	if (pdev->activate(pdev)<0) {
		snd_printk("isapnp configure failure (out of resources?)\n");
		return -EBUSY;
	}
	snd_sb_port[dev] = pdev->resource[0].start;
	snd_wss_port[dev] = pdev->resource[1].start;
	snd_fm_port[dev] = pdev->resource[2].start;
	snd_midi_port[dev] = pdev->resource[3].start;
	snd_port[dev] = pdev->resource[4].start;
	snd_dma1[dev] = pdev->dma_resource[0].start;
	snd_dma2[dev] = pdev->dma_resource[1].start;
	snd_irq[dev] = pdev->irq_resource[0].start;
	snd_printdd("isapnp OPL3-SA: sb port=0x%lx, wss port=0x%lx, fm port=0x%lx, midi port=0x%lx\n",
		snd_sb_port[dev], snd_wss_port[dev], snd_fm_port[dev], snd_midi_port[dev]);
	snd_printdd("isapnp OPL3-SA: control port=0x%lx, dma1=%i, dma2=%i, irq=%i\n",
		snd_port[dev], snd_dma1[dev], snd_dma2[dev], snd_irq[dev]);
	return 0;
}

static void snd_opl3sa_deactivate(struct snd_opl3sa *oplcard)
{
	if (oplcard->dev) {
		oplcard->dev->deactivate(oplcard->dev);
		oplcard->dev = NULL;
	}
}
#endif /* __ISAPNP__ */

static void snd_card_opl3sa2_free(snd_card_t *card)
{
	struct snd_opl3sa *acard = (struct snd_opl3sa *)card->private_data;

	if (acard) {
#ifdef __ISAPNP__
		snd_opl3sa_deactivate(acard);
#endif
		if (acard->irq >= 0)
			free_irq(acard->irq, (void *)acard);
		if (acard->res_port)
			release_resource(acard->res_port);
	}
}

static int __init snd_opl3sa_probe(int dev)
{
	int irq, dma1, dma2;
	snd_card_t *card;
	struct snd_opl3sa *oplcard;
	cs4231_t *cs4231;
	opl3_t *opl3;
	int err;

#ifdef __ISAPNP__
	if (!snd_isapnp[dev]) {
#endif
		if (snd_port[dev] == SNDRV_AUTO_PORT) {
			snd_printk("specify snd_port\n");
			return -EINVAL;
		}
		if (snd_wss_port[dev] == SNDRV_AUTO_PORT) {
			snd_printk("specify snd_wss_port\n");
			return -EINVAL;
		}
		if (snd_fm_port[dev] == SNDRV_AUTO_PORT) {
			snd_printk("specify snd_fm_port\n");
			return -EINVAL;
		}
		if (snd_midi_port[dev] == SNDRV_AUTO_PORT) {
			snd_printk("specify snd_midi_port\n");
			return -EINVAL;
		}
#ifdef __ISAPNP__
	}
#endif
	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE,
			    sizeof(struct snd_opl3sa));
	if (card == NULL)
		return -ENOMEM;
	oplcard = (struct snd_opl3sa *)card->private_data;
	oplcard->irq = -1;
	card->private_free = snd_card_opl3sa2_free;
	strcpy(card->driver, "OPL3SA2");
	strcpy(card->shortname, "Yamaha OPL3-SA2");
#ifdef __ISAPNP__
	if (snd_isapnp[dev] && snd_opl3sa_isapnp(dev, oplcard) < 0) {
		snd_card_free(card);
		return -EBUSY;
	}
#endif
	oplcard->ymode = snd_opl3sa3_ymode[dev] & 0x03 ; /* SL Added - initialise this card from supplied (or default) parameter*/ 
	oplcard->card = card;
	oplcard->port = snd_port[dev];
	irq = snd_irq[dev];
	dma1 = snd_dma1[dev];
	dma2 = snd_dma2[dev];
	if (dma2 < 0)
		oplcard->single_dma = 1;
	if ((snd_opl3sa_detect(oplcard)) < 0) {
		snd_card_free(card);
		return -ENODEV;
	}
	if (request_irq(irq, snd_opl3sa_interrupt, SA_INTERRUPT, "OPL3-SA2/3", (void *)oplcard)) {
		snd_card_free(card);
		return -ENODEV;
	}
	oplcard->irq = irq;
	if ((err = snd_cs4231_create(card,
				     snd_wss_port[dev] + 4, -1,
				     irq, dma1, dma2,
				     CS4231_HW_OPL3SA2,
				     CS4231_HWSHARE_IRQ,
				     &cs4231)) < 0) {
		snd_printd("Oops, WSS not detected at 0x%lx\n", snd_wss_port[dev] + 4);
		snd_card_free(card);
		return err;
	}
	oplcard->cs4231 = cs4231;
	if ((err = snd_cs4231_pcm(cs4231, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_mixer(cs4231)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_opl3sa_mixer(oplcard)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_timer(cs4231, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (snd_fm_port[dev] >= 0x340 && snd_fm_port[dev] < 0x400) {
		if ((err = snd_opl3_create(card, snd_fm_port[dev],
					   snd_fm_port[dev] + 2,
					   OPL3_HW_OPL3, 0, &opl3)) < 0) {
			snd_card_free(card);
			return err;
		}
		if ((err = snd_opl3_timer_new(opl3, 1, 2)) < 0) {
			snd_card_free(card);
			return err;
		}
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, &oplcard->synth)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if (snd_midi_port[dev] >= 0x300 && snd_midi_port[dev] < 0x340) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_OPL3SA2,
					       snd_midi_port[dev], 0,
					       irq, 0, &oplcard->rmidi)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
		card->shortname, oplcard->port, irq, dma1);
	if (dma2 >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", dma2);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_opl3sa_cards[dev] = card;
	return 0;
}

#ifdef __ISAPNP__
static int __init snd_opl3sa2_isapnp_detect(struct isapnp_card *card,
					    const struct isapnp_card_id *id)
{
        static int dev = 0;
        int res;

        for ( ; dev < SNDRV_CARDS; dev++) {
                if (!snd_enable[dev])
                        continue;
                snd_opl3sa2_isapnp_cards[dev] = card;
                snd_opl3sa2_isapnp_id[dev] = id;
                res = snd_opl3sa_probe(dev);
                if (res < 0)
                        return res;
                dev++;
                return 0;
        }
        return -ENODEV;
}
#endif /* __ISAPNP__ */

static int __init alsa_card_opl3sa2_init(void)
{
	int dev, cards = 0;

	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev])
			continue;
#ifdef __ISAPNP__
		if (snd_isapnp[dev])
			continue;
#endif
		if (snd_opl3sa_probe(dev) >= 0)
			cards++;
	}
#ifdef __ISAPNP__
	cards += isapnp_probe_cards(snd_opl3sa2_pnpids, snd_opl3sa2_isapnp_detect);
#endif
	if (!cards) {
#ifdef MODULE
		snd_printk("Yamaha OPL3-SA soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_opl3sa2_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_opl3sa_cards[idx]);
}

module_init(alsa_card_opl3sa2_init)
module_exit(alsa_card_opl3sa2_exit)

#ifndef MODULE

/* format is: snd-card-opl3sa2=snd_enable,snd_index,snd_id,snd_isapnp,
			       snd_port,snd_sb_port,snd_wss_port,snd_fm_port,
			       snd_midi_port,snd_irq,snd_dma1,snd_dma2,
			       snd_opl3sa3_ymode */

static int __init alsa_card_opl3sa2_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;
	int __attribute__ ((__unused__)) pnp = INT_MAX;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,&pnp) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_sb_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_wss_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_fm_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_midi_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma1[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma2[nr_dev]) == 2 &&
	       get_option(&str,&snd_opl3sa3_ymode[nr_dev]) == 2);
#ifdef __ISAPNP__
	if (pnp != INT_MAX)
		snd_isapnp[nr_dev] = pnp;
#endif
	nr_dev++;
	return 1;
}

__setup("snd-card-opl3sa2=", alsa_card_opl3sa2_setup);

#endif /* ifndef MODULE */
