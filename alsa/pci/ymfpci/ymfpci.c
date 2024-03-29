/*
 *  The driver for the Yamaha's DS1/DS1E cards
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
#include <sound/ymfpci.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("Yamaha DS-XG PCI");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Yamaha,YMF724},"
		"{Yamaha,YMF724F},"
		"{Yamaha,YMF740},"
		"{Yamaha,YMF740C},"
		"{Yamaha,YMF744},"
		"{Yamaha,YMF754}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
#ifdef TARGET_OS2
static long snd_fm_port[SNDRV_CARDS] = {REPEAT_SNDRV(-1)};
static long snd_mpu_port[SNDRV_CARDS] = {REPEAT_SNDRV(-1)};
static int snd_mpu_irq[SNDRV_CARDS] = {REPEAT_SNDRV(-1) };
#else
static long snd_fm_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
static long snd_mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
static int snd_mpu_irq[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1 };
#endif

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for the Yamaha DS-XG PCI soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for the Yamaha DS-XG PCI soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable Yamaha DS-XG soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_mpu_port, "MPU-401 Port.");
MODULE_PARM_SYNTAX(snd_mpu_port, SNDRV_ENABLED);
MODULE_PARM(snd_mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_mpu_irq, "MPU-401 IRQ number.");
MODULE_PARM_SYNTAX(snd_mpu_irq, SNDRV_ENABLED ",allows:{{5},{7},{9},{10},{11}},dialog:list");
MODULE_PARM(snd_fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_fm_port, "FM OPL-3 Port.");
MODULE_PARM_SYNTAX(snd_fm_port, SNDRV_ENABLED);

static struct pci_device_id snd_ymfpci_ids[] __devinitdata = {
        { 0x1073, 0x0004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF724 */
        { 0x1073, 0x000d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF724F */
        { 0x1073, 0x000a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF740 */
        { 0x1073, 0x000c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF740C */
        { 0x1073, 0x0010, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF744 */
        { 0x1073, 0x0012, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF754 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_ymfpci_ids);

static int __init snd_card_ymfpci_probe(struct pci_dev *pci,
					const struct pci_device_id *id)
{
	static int dev = 0;
	snd_card_t *card;
	ymfpci_t *chip;
	opl3_t *opl3;
	char *str;
	int err;
	u16 legacy_ctrl, legacy_ctrl2, old_legacy_ctrl;

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

	switch (id->device) {
	case 0x0004: str = "YMF724"; break;
	case 0x000d: str = "YMF724F"; break;
	case 0x000a: str = "YMF740"; break;
	case 0x000c: str = "YMF740C"; break;
	case 0x0010: str = "YMF744"; break;
	case 0x0012: str = "YMF754"; break;
	default: str = "???"; break;
	}

	legacy_ctrl = 0;
	legacy_ctrl2 = 0;

	if (id->device >= 0x0010) { /* YMF 744/754 */
		if (snd_fm_port[dev] < 0)
			snd_fm_port[dev] = pci_resource_start(pci, 1);
		else if (check_region(snd_fm_port[dev], 4))
			snd_fm_port[dev] = -1;
		if (snd_fm_port[dev] >= 0) {
			legacy_ctrl |= 2;
			pci_write_config_word(pci, 0x60, snd_fm_port[dev]);
		}
		if (snd_mpu_port[dev] < 0)
			snd_mpu_port[dev] = pci_resource_start(pci, 1) + 0x20;
		else if (check_region(snd_mpu_port[dev], 2))
			snd_mpu_port[dev] = -1;
		if (snd_mpu_port[dev] >= 0) {
			legacy_ctrl |= 8;
			pci_write_config_word(pci, 0x62, snd_fm_port[dev]);
		}
#if 0
		sprintf(str1, "%s joystick", str);
		if (snd_joystick_port[dev] < 0)
			snd_joystick_port[dev] = pci_resource_start(pci, 2);
		else if (check_region(snd_joystick_port[dev], 4))
			snd_joystick_port[dev] = -1;
		if (snd_joystick_port[dev] >= 0) {
			legacy_ctrl |= 4;
			pci_write_config_word(pci, 0x66, snd_joystick_port[dev]);
		}
#endif
	} else {
		switch (snd_fm_port[dev]) {
		case 0x388: legacy_ctrl2 |= 0; break;
		case 0x398: legacy_ctrl2 |= 1; break;
		case 0x3a0: legacy_ctrl2 |= 2; break;
		case 0x3a8: legacy_ctrl2 |= 3; break;
		default: snd_fm_port[dev] = -1; break;
		}
		if (snd_fm_port[dev] > 0 && check_region(snd_fm_port[dev], 4) == 0)
			legacy_ctrl |= 2;
		else {
			legacy_ctrl2 &= ~3;
			snd_fm_port[dev] = -1;
		}
		switch (snd_mpu_port[dev]) {
		case 0x330: legacy_ctrl2 |= 0 << 4; break;
		case 0x300: legacy_ctrl2 |= 1 << 4; break;
		case 0x332: legacy_ctrl2 |= 2 << 4; break;
		case 0x334: legacy_ctrl2 |= 3 << 4; break;
		default: snd_mpu_port[dev] = -1; break;
		}
		if (snd_mpu_port[dev] > 0 && check_region(snd_mpu_port[dev], 2) == 0)
			legacy_ctrl |= 8;
		else {
			legacy_ctrl2 &= ~(3 << 4);
			snd_mpu_port[dev] = -1;
		}
#if 0
		switch (snd_joystick_port[dev]) {
		case 0x201: legacy_ctrl2 |= 0 << 6; break;
		case 0x202: legacy_ctrl2 |= 1 << 6; break;
		case 0x204: legacy_ctrl2 |= 2 << 6; break;
		case 0x205: legacy_ctrl2 |= 3 << 6; break;
		default: snd_joystick_port[dev] = -1; break;
		}
		if (snd_joystick_port[dev] > 0 && check_region(snd_joystick_port[dev], 2) == 0)
			legacy_ctrl |= 4;
		else {
			legacy_ctrl2 &= ~(3 << 6);
			snd_joystick_port[dev] = -1;
		}
#endif
	}
	pci_read_config_word(pci, 0x40, &old_legacy_ctrl);
	switch (snd_mpu_irq[dev]) {
	case 5:	break;
	case 7:	legacy_ctrl |= 1 << 11; break;
	case 9: legacy_ctrl |= 2 << 11; break;
	case 10: legacy_ctrl |= 3 << 11; break;
	case 11: legacy_ctrl |= 4 << 11; break;
	default: snd_mpu_irq[dev] = -1; break;
	}
	legacy_ctrl |= (snd_mpu_irq[dev] > 0 ? 0x10 : 0);	/* MPU401 IRQ enable */
	snd_printdd("legacy_ctrl = 0x%x\n", legacy_ctrl);
	pci_write_config_word(pci, 0x40, legacy_ctrl);
	snd_printdd("legacy_ctrl2 = 0x%x\n", legacy_ctrl2);
	pci_write_config_word(pci, 0x42, legacy_ctrl2);
	if ((err = snd_ymfpci_create(card, pci,
				     old_legacy_ctrl,
			 	     &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_pcm_spdif(chip, 1, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_pcm_4ch(chip, 2, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_pcm2(chip, 3, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (snd_mpu_port[dev] > 0) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_YMFPCI,
					       snd_mpu_port[dev], 0,
					       snd_mpu_irq[dev] > 0 ? snd_mpu_irq[dev] : -1, SA_INTERRUPT, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if (snd_fm_port[dev] > 0) {
		if ((err = snd_opl3_create(card,
					   snd_fm_port[dev],
					   snd_fm_port[dev] + 2,
					   OPL3_HW_OPL3, 0, &opl3)) < 0) {
			snd_card_free(card);
			return err;
		}
		if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	strcpy(card->driver, str);
	sprintf(card->shortname, "Yamaha DS-XG PCI (%s)", str);
	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname,
		chip->reg_area_virt,
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
static int snd_card_ymfpci_suspend(struct pci_dev *pci, u32 state)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, PCI_GET_DRIVER_DATA(pci), return -ENXIO);
	snd_ymfpci_suspend(chip);
	return 0;
}
static int snd_card_ymfpci_resume(struct pci_dev *pci)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, PCI_GET_DRIVER_DATA(pci), return -ENXIO);
	snd_ymfpci_resume(chip);
	return 0;
}
#else
static void snd_card_ymfpci_suspend(struct pci_dev *pci)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, PCI_GET_DRIVER_DATA(pci), return);
	snd_ymfpci_suspend(chip);
}
static void snd_card_ymfpci_resume(struct pci_dev *pci)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, PCI_GET_DRIVER_DATA(pci), return);
	snd_ymfpci_resume(chip);
}
#endif
#endif

static void __exit snd_card_ymfpci_remove(struct pci_dev *pci)
{
	ymfpci_t *chip = snd_magic_cast(ymfpci_t, PCI_GET_DRIVER_DATA(pci), return);
	if (chip)
		snd_card_free(chip->card);
	PCI_SET_DRIVER_DATA(pci, NULL);
}

#ifdef TARGET_OS2
static struct pci_driver driver = {
	0,0,"Yamaha DS-XG PCI",
	snd_ymfpci_ids,
	snd_card_ymfpci_probe,
	snd_card_ymfpci_remove,
#ifdef CONFIG_PM
	snd_card_ymfpci_suspend,
	snd_card_ymfpci_resume,
#else 
        0,0
#endif	
};
#else
static struct pci_driver driver = {
	name: "Yamaha DS-XG PCI",
	id_table: snd_ymfpci_ids,
	probe: snd_card_ymfpci_probe,
	remove: snd_card_ymfpci_remove,
#ifdef CONFIG_PM
	suspend: snd_card_ymfpci_suspend,
	resume: snd_card_ymfpci_resume,
#endif	
};
#endif

static int snd_ymfpci_notifier(struct notifier_block *nb, unsigned long event, void *buf)
{
	pci_unregister_driver(&driver);
	return NOTIFY_OK;
}

static struct notifier_block snd_ymfpci_nb = {snd_ymfpci_notifier, NULL, 0};

static int __init alsa_card_ymfpci_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		snd_printk("Yamaha DS-XG PCI soundcard not found or device busy\n");
#endif
		return err;
	}
	/* If this driver is not shutdown cleanly at reboot, it can
	   leave the speaking emitting an annoying noise, so we catch
	   shutdown events. */ 
	if (register_reboot_notifier(&snd_ymfpci_nb)) {
		snd_printk("reboot notifier registration failed; may make noise at shutdown.\n");
	}
	return 0;
}

static void __exit alsa_card_ymfpci_exit(void)
{
	unregister_reboot_notifier(&snd_ymfpci_nb);
	pci_unregister_driver(&driver);
}

module_init(alsa_card_ymfpci_init)
module_exit(alsa_card_ymfpci_exit)

#ifndef MODULE

/* format is: snd-card-ymfpci=snd_enable,snd_index,snd_id,
			      snd_fm_port,snd_mpu_port,snd_mpu_irq */

static int __init alsa_card_ymfpci_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_fm_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_mpu_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_mpu_irq[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-ymfpci=", alsa_card_ymfpci_setup);

#endif /* ifndef MODULE */
