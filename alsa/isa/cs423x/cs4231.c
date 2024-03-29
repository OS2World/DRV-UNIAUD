/*
 *  Generic driver for CS4231 chips
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Originally the CS4232/CS4232A driver, modified for use on CS4231 by
 *  Tugrul Galatali <galatalt@stuy.edu>
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
#define SNDRV_GET_ID
#include <sound/initval.h>

#define chip_t cs4231_t

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("Generic CS4231");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Crystal Semiconductors,CS4231}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long snd_mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,11,12,15 */
static int snd_mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 9,11,12,15 */
static int snd_dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */
static int snd_dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for CS4231 soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for CS4231 soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable CS4231 soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for CS4231 driver.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_mpu_port, "MPU-401 port # for CS4231 driver.");
MODULE_PARM_SYNTAX(snd_mpu_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for CS4231 driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_IRQ_DESC);
MODULE_PARM(snd_mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_mpu_irq, "MPU-401 IRQ # for CS4231 driver.");
MODULE_PARM_SYNTAX(snd_mpu_irq, SNDRV_IRQ_DESC);
MODULE_PARM(snd_dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma1, "DMA1 # for CS4231 driver.");
MODULE_PARM_SYNTAX(snd_dma1, SNDRV_DMA_DESC);
MODULE_PARM(snd_dma2, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma2, "DMA2 # for CS4231 driver.");
MODULE_PARM_SYNTAX(snd_dma2, SNDRV_DMA_DESC);

static snd_card_t *snd_cs4231_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_card_cs4231_probe(int dev)
{
	snd_card_t *card;
	struct snd_card_cs4231 *acard;
	snd_pcm_t *pcm = NULL;
	cs4231_t *chip;
	int err;

	if (snd_port[dev] == SNDRV_AUTO_PORT) {
		snd_printk("specify snd_port\n");
		return -EINVAL;
	}
	if (snd_irq[dev] == SNDRV_AUTO_IRQ) {
		snd_printk("specify snd_irq\n");
		return -EINVAL;
	}
	if (snd_dma1[dev] == SNDRV_AUTO_DMA) {
		snd_printk("specify snd_dma1\n");
		return -EINVAL;
	}
	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	acard = (struct snd_card_cs4231 *)card->private_data;
	if (snd_mpu_port[dev] < 0)
		snd_mpu_port[dev] = SNDRV_AUTO_PORT;
	if ((err = snd_cs4231_create(card, snd_port[dev], -1,
				     snd_irq[dev],
				     snd_dma1[dev],
				     snd_dma2[dev],
				     CS4231_HW_DETECT,
				     0, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_cs4231_pcm(chip, 0, &pcm)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4231_timer(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (snd_mpu_irq[dev] >= 0 && snd_mpu_irq[dev] != SNDRV_AUTO_IRQ) {
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_CS4232,
					snd_mpu_port[dev], 0,
					snd_mpu_irq[dev], SA_INTERRUPT,
					NULL) < 0)
			snd_printk("MPU401 not detected\n");
	}
	strcpy(card->driver, "CS4231");
	strcpy(card->shortname, pcm->name);
	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
		pcm->name, chip->port, snd_irq[dev], snd_dma1[dev]);
	if (snd_dma2[dev] >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", snd_dma2[dev]);
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_cs4231_cards[dev] = card;
	return 0;
}

static int __init alsa_card_cs4231_init(void)
{
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && snd_enable[dev]; dev++) {
		if (snd_card_cs4231_probe(dev) >= 0)
			cards++;
	}
	if (!cards) {
#ifdef MODULE
		snd_printk("CS4231 soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_cs4231_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_cs4231_cards[idx]);
}

module_init(alsa_card_cs4231_init)
module_exit(alsa_card_cs4231_exit)

#ifndef MODULE

/* format is: snd-card-cs4231=snd_enable,snd_index,snd_id,
			      snd_port,snd_mpu_port,snd_irq,snd_mpu_irq,
			      snd_dma1,snd_dma2 */

static int __init alsa_card_cs4231_setup(char *str)
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
	       get_option(&str,(int *)&snd_mpu_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_mpu_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma1[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma2[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-cs4231=", alsa_card_cs4231_setup);

#endif /* ifndef MODULE */
