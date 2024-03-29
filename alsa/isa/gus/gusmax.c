/*
 *  Driver for Gravis UltraSound MAX soundcard
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
#include <sound/gus.h>
#include <sound/cs4231.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("Gravis UltraSound MAX");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Gravis,UltraSound MAX}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x230,0x240,0x250,0x260 */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 2,3,5,9,11,12,15 */
static int snd_dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
static int snd_dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 1,3,5,6,7 */
#ifdef TARGET_OS2
static int snd_joystick_dac[SNDRV_CARDS] = {REPEAT_SNDRV(29)};
				/* 0 to 31, (0.59V-4.52V or 0.389V-2.98V) */
static int snd_channels[SNDRV_CARDS] = {REPEAT_SNDRV(24)};
static int snd_pcm_channels[SNDRV_CARDS] = {REPEAT_SNDRV(2)};
#else
static int snd_joystick_dac[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 29};
				/* 0 to 31, (0.59V-4.52V or 0.389V-2.98V) */
static int snd_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 24};
static int snd_pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};
#endif

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for GUS MAX soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for GUS MAX soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable GUS MAX soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for GUS MAX driver.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_ENABLED ",allows:{{0x220},{0x230},{0x240},{0x250},{0x260}},dialog:list");
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for GUS MAX driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_ENABLED ",allows:{{3},{5},{9},{11},{12},{15}},dialog:list");
MODULE_PARM(snd_dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma1, "DMA1 # for GUS MAX driver.");
MODULE_PARM_SYNTAX(snd_dma1, SNDRV_DMA_DESC);
MODULE_PARM(snd_dma2, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma2, "DMA2 # for GUS MAX driver.");
MODULE_PARM_SYNTAX(snd_dma2, SNDRV_DMA_DESC);
MODULE_PARM(snd_joystick_dac, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_joystick_dac, "Joystick DAC level 0.59V-4.52V or 0.389V-2.98V for GUS MAX driver.");
MODULE_PARM_SYNTAX(snd_joystick_dac, SNDRV_ENABLED ",allows:{{0,31}}");
MODULE_PARM(snd_channels, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_channels, "Used GF1 channels for GUS MAX driver.");
MODULE_PARM_SYNTAX(snd_channels, SNDRV_ENABLED ",allows:{{14,32}}");
MODULE_PARM(snd_pcm_channels, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_pcm_channels, "Reserved PCM channels for GUS MAX driver.");
MODULE_PARM_SYNTAX(snd_pcm_channels, SNDRV_ENABLED ",allows:{{2,16}}");

struct snd_gusmax {
	int irq;
	snd_card_t *card;
	snd_gus_card_t *gus;
	cs4231_t *cs4231;
	unsigned short gus_status_reg;
	unsigned short pcm_status_reg;
};

static snd_card_t *snd_gusmax_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_gusmax_detect(snd_gus_card_t * gus)
{
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 0);	/* reset GF1 */
#ifdef CONFIG_SND_DEBUG_DETECT
	{
		unsigned char d;

		if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 0) {
			snd_printk("[0x%lx] check 1 failed - 0x%x\n", gus->gf1.port, d);
			return -ENODEV;
		}
	}
#else
	if ((snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET) & 0x07) != 0)
		return -ENODEV;
#endif
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 1);	/* release reset */
	udelay(160);
#ifdef CONFIG_SND_DEBUG_DETECT
	{
		unsigned char d;

		if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 1) {
			snd_printk("[0x%lx] check 2 failed - 0x%x\n", gus->gf1.port, d);
			return -ENODEV;
		}
	}
#else
	if ((snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET) & 0x07) != 1)
		return -ENODEV;
#endif
	return 0;
}

static void snd_gusmax_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct snd_gusmax *maxcard = (struct snd_gusmax *) dev_id;
	int loop, max = 5;

	do {
		loop = 0;
		if (inb(maxcard->gus_status_reg)) {
			snd_gus_interrupt(irq, maxcard->gus, regs);
			loop++;
		}
		if (inb(maxcard->pcm_status_reg) & 0x01) { /* IRQ bit is set? */
			snd_cs4231_interrupt(irq, maxcard->cs4231, regs);
			loop++;
		}
	} while (loop && --max > 0);
}

static void __init snd_gusmax_init(int dev, snd_card_t * card, snd_gus_card_t * gus)
{
	gus->equal_irq = 1;
	gus->codec_flag = 1;
	gus->joystick_dac = snd_joystick_dac[dev];
	/* init control register */
	gus->max_cntrl_val = (gus->gf1.port >> 4) & 0x0f;
	if (gus->gf1.dma1 > 3)
		gus->max_cntrl_val |= 0x10;
	if (gus->gf1.dma2 > 3)
		gus->max_cntrl_val |= 0x20;
	gus->max_cntrl_val |= 0x40;
	outb(gus->max_cntrl_val, GUSP(gus, MAXCNTRLPORT));
}

#define CS4231_PRIVATE( left, right, shift, mute ) \
			((left << 24)|(right << 16)|(shift<<8)|mute)

static int __init snd_gusmax_mixer(cs4231_t *chip)
{
	snd_card_t *card = chip->card;
	snd_ctl_elem_id_t id1, id2;
	int err;
	
	memset(&id1, 0, sizeof(id1));
	memset(&id2, 0, sizeof(id2));
	id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	/* reassign AUXA to SYNTHESIZER */
	strcpy(id1.name, "Aux Playback Switch");
	strcpy(id2.name, "Synth Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "Synth Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	/* reassign AUXB to CD */
	strcpy(id1.name, "Aux Playback Switch"); id1.index = 1;
	strcpy(id2.name, "CD Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "CD Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
#if 0
	/* reassign Mono Input to MIC */
	if (snd_mixer_group_rename(mixer,
				SNDRV_MIXER_IN_MONO, 0,
				SNDRV_MIXER_IN_MIC, 0) < 0)
		goto __error;
	if (snd_mixer_elem_rename(mixer,
				SNDRV_MIXER_IN_MONO, 0, SNDRV_MIXER_ETYPE_INPUT,
				SNDRV_MIXER_IN_MIC, 0) < 0)
		goto __error;
	if (snd_mixer_elem_rename(mixer,
				"Mono Capture Volume", 0, SNDRV_MIXER_ETYPE_VOLUME1,
				"Mic Capture Volume", 0) < 0)
		goto __error;
	if (snd_mixer_elem_rename(mixer,
				"Mono Capture Switch", 0, SNDRV_MIXER_ETYPE_SWITCH1,
				"Mic Capture Switch", 0) < 0)
		goto __error;
#endif
	return 0;
}

static void snd_gusmax_free(snd_card_t *card)
{
	struct snd_gusmax *maxcard = (struct snd_gusmax *)card->private_data;
	
	if (maxcard == NULL)
		return;
	if (maxcard->irq >= 0)
		free_irq(maxcard->irq, (void *)maxcard);
}

static int __init snd_gusmax_probe(int dev)
{
	static int possible_irqs[] = {5, 11, 12, 9, 7, 15, 3, -1};
	static int possible_dmas[] = {5, 6, 7, 1, 3, -1};
	int irq, dma1, dma2, err;
	snd_card_t *card;
	snd_gus_card_t *gus = NULL;
	cs4231_t *cs4231;
	struct snd_gusmax *maxcard;

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE,
			    sizeof(struct snd_gusmax));
	if (card == NULL)
		return -ENOMEM;
	card->private_free = snd_gusmax_free;
	maxcard = (struct snd_gusmax *)card->private_data;
	maxcard->card = card;
	maxcard->irq = -1;
	
	irq = snd_irq[dev];
	if (irq == SNDRV_AUTO_IRQ) {
		if ((irq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	dma1 = snd_dma1[dev];
	if (dma1 == SNDRV_AUTO_DMA) {
		if ((dma1 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free DMA1\n");
			return -EBUSY;
		}
	}
	dma2 = snd_dma2[dev];
	if (dma2 == SNDRV_AUTO_DMA) {
		if ((dma2 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_card_free(card);
			snd_printk("unable to find a free DMA2\n");
			return -EBUSY;
		}
	}

	if ((err = snd_gus_create(card,
				  snd_port[dev],
				  -irq, dma1, dma2,
				  0, snd_channels[dev],
				  snd_pcm_channels[dev],
				  0, &gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_gusmax_detect(gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	maxcard->gus_status_reg = gus->gf1.reg_irqstat;
	maxcard->pcm_status_reg = gus->gf1.port + 0x10c + 2;
	snd_gusmax_init(dev, card, gus);
	if ((err = snd_gus_initialize(gus)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (!gus->max_flag) {
		snd_card_free(card);
		snd_printk("GUS MAX soundcard was not detected at 0x%lx\n", gus->gf1.port);
		return -ENODEV;
	}

	if (request_irq(irq, snd_gusmax_interrupt, SA_INTERRUPT, "GUS MAX", (void *)maxcard)) {
		snd_card_free(card);
		snd_printk("unable to grab IRQ %d\n", irq);
		return -EBUSY;
	}
	maxcard->irq = irq;
	
	if ((err = snd_cs4231_create(card,
				     gus->gf1.port + 0x10c, -1, irq,
				     dma2 < 0 ? dma1 : dma2, dma1,
				     CS4231_HW_DETECT,
				     CS4231_HWSHARE_IRQ |
				     CS4231_HWSHARE_DMA1 |
				     CS4231_HWSHARE_DMA2,
				     &cs4231)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_pcm(cs4231, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_mixer(cs4231)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_timer(cs4231, 2, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (snd_pcm_channels[dev] > 0) {
		if ((err = snd_gf1_pcm_new(gus, 1, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if ((err = snd_gusmax_mixer(cs4231)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_gf1_rawmidi_new(gus, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}

	sprintf(card->longname + strlen(card->longname), " at 0x%lx, irq %i, dma %i", gus->gf1.port, irq, dma1);
	if (dma2 >= 0)
		sprintf(card->longname + strlen(card->longname), "&%i", dma2);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
		
	maxcard->gus = gus;
	maxcard->cs4231 = cs4231;
	snd_gusmax_cards[dev] = card;
	return 0;
}

static int __init snd_gusmax_legacy_auto_probe(unsigned long port)
{
	static int dev = 0;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev] || snd_port[dev] != SNDRV_AUTO_PORT)
			continue;
		snd_port[dev] = port;
		res = snd_gusmax_probe(dev);
		if (res < 0)
			snd_port[dev] = SNDRV_AUTO_PORT;
		return res;
	}
	return -ENODEV;
}

static int __init alsa_card_gusmax_init(void)
{
	static unsigned long possible_ports[] = {0x220, 0x230, 0x240, 0x250, 0x260, -1};
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && snd_enable[dev] > 0; dev++) {
		if (snd_port[dev] == SNDRV_AUTO_PORT)
			continue;
		if (snd_gusmax_probe(dev) >= 0)
			cards++;
	}
	cards += snd_legacy_auto_probe(possible_ports, snd_gusmax_legacy_auto_probe);
	if (!cards) {
#ifdef MODULE
		snd_printk("GUS MAX soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_gusmax_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_gusmax_cards[idx]);
}

module_init(alsa_card_gusmax_init)
module_exit(alsa_card_gusmax_exit)

#ifndef MODULE

/* format is: snd-card-gusmax=snd_enable,snd_index,snd_id,
			      snd_port,snd_irq,
			      snd_dma1,snd_dma2,
			      snd_joystick_dac,
			      snd_channels,snd_pcm_channels */

static int __init alsa_card_gusmax_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma1[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma2[nr_dev]) == 2 &&
	       get_option(&str,&snd_joystick_dac[nr_dev]) == 2 &&
	       get_option(&str,&snd_channels[nr_dev]) == 2 &&
	       get_option(&str,&snd_pcm_channels[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-gusmax=", alsa_card_gusmax_setup);

#endif /* ifndef MODULE */
