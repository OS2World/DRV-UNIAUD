/*
 *  The driver for the Cirrus Logic's Sound Fusion CS46XX based soundcards
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

/*
  NOTES:
  - sometimes the sound is metallic and sibilant, unloading and 
    reloading the module may solve this.
*/

#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#include <sound/cs46xx.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("Cirrus Logic Sound Fusion CS46XX");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Cirrus Logic,Sound Fusion (CS4280)},"
		"{Cirrus Logic,Sound Fusion (CS4610)},"
		"{Cirrus Logic,Sound Fusion (CS4612)},"
		"{Cirrus Logic,Sound Fusion (CS4615)},"
		"{Cirrus Logic,Sound Fusion (CS4622)},"
		"{Cirrus Logic,Sound Fusion (CS4624)},"
		"{Cirrus Logic,Sound Fusion (CS4630)}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
#ifdef TARGET_OS2
static int snd_external_amp[SNDRV_CARDS] = {0};
static int snd_thinkpad[SNDRV_CARDS] = {0};
#else
static int snd_external_amp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
static int snd_thinkpad[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
#endif

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for the CS46xx soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for the CS46xx soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable CS46xx soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_external_amp, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_external_amp, "Force to enable external amplifer.");
MODULE_PARM_SYNTAX(snd_external_amp, SNDRV_BOOLEAN_FALSE_DESC);
MODULE_PARM(snd_thinkpad, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_thinkpad, "Force to enable Thinkpad's CLKRUN control.");
MODULE_PARM_SYNTAX(snd_thinkpad, SNDRV_BOOLEAN_FALSE_DESC);

static struct pci_device_id snd_cs46xx_ids[] __devinitdata = {
        { 0x1013, 0x6001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* CS4280 */
        { 0x1013, 0x6003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* CS4612 */
        { 0x1013, 0x6004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* CS4615 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_cs46xx_ids);

static int __init snd_card_cs46xx_probe(struct pci_dev *pci,
					const struct pci_device_id *id)
{
	static int dev = 0;
	snd_card_t *card;
	cs46xx_t *chip;
	int err;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev]) {
			dev++;
			return -ENOENT;
		}
		break;
	}
	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	if ((err = snd_cs46xx_create(card, pci,
				     snd_external_amp[dev], snd_thinkpad[dev],
				     &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs46xx_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs46xx_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs46xx_midi(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	strcpy(card->driver, "CS46xx");
	strcpy(card->shortname, "Sound Fusion CS46xx");
	sprintf(card->longname, "%s at 0x%lx/0x%lx, irq %i",
		card->shortname,
		chip->ba0_addr,
		chip->ba1_addr,
		chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	PCI_SET_DRIVER_DATA(pci, chip);
	dev++;
	return 0;
}

#ifdef CONFIG_PM
#ifdef PCI_NEW_SUSPEND
static int snd_card_cs46xx_suspend(struct pci_dev *pci, u32 state)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, PCI_GET_DRIVER_DATA(pci), return -ENXIO);
	snd_cs46xx_suspend(chip);
	return 0;
}
static int snd_card_cs46xx_resume(struct pci_dev *pci)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, PCI_GET_DRIVER_DATA(pci), return -ENXIO);
	snd_cs46xx_resume(chip);
	return 0;
}
#else
static void snd_card_cs46xx_suspend(struct pci_dev *pci)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, PCI_GET_DRIVER_DATA(pci), return);
	snd_cs46xx_suspend(chip);
}
static void snd_card_cs46xx_resume(struct pci_dev *pci)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, PCI_GET_DRIVER_DATA(pci), return);
	snd_cs46xx_resume(chip);
}
#endif
#endif

static void __exit snd_card_cs46xx_remove(struct pci_dev *pci)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, PCI_GET_DRIVER_DATA(pci), return);
	if (chip)
		snd_card_free(chip->card);
	PCI_SET_DRIVER_DATA(pci, NULL);
}

#ifdef TARGET_OS2
static struct pci_driver driver = {
        0, 0,
	"Sound Fusion CS46xx",
	snd_cs46xx_ids,
	snd_card_cs46xx_probe,
	snd_card_cs46xx_remove,
#ifdef CONFIG_PM
	snd_card_cs46xx_suspend,
	snd_card_cs46xx_resume,
#else
        0, 0
#endif
};
#else
static struct pci_driver driver = {
	name: "Sound Fusion CS46xx",
	id_table: snd_cs46xx_ids,
	probe: snd_card_cs46xx_probe,
	remove: snd_card_cs46xx_remove,
#ifdef CONFIG_PM
	suspend: snd_card_cs46xx_suspend,
	resume: snd_card_cs46xx_resume,
#endif
};
#endif

static int __init alsa_card_cs46xx_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		snd_printk("Sound Fusion CS46xx soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_cs46xx_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_cs46xx_init)
module_exit(alsa_card_cs46xx_exit)

#ifndef MODULE

/* format is: snd-card-cs46xx=snd_enable,snd_index,snd_id */

static int __init alsa_card_cs46xx_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-cs46xx=", alsa_card_cs46xx_setup);

#endif /* ifndef MODULE */
