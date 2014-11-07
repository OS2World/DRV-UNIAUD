/*
 *  Driver for Trident 4DWave DX/NX & SiS SI7018 Audio PCI soundcard
 *
 *  Driver was originated by Trident <audio@tridentmicro.com>
 *  			     Fri Feb 19 15:55:28 MST 1999
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
#include <sound/trident.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("Trident 4D-WaveDX/NX & SiS SI7018");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Trident,4DWave DX},"
		"{Trident,4DWave NX},"
		"{SiS,SI7018 PCI Audio},"
		"{Best Union,Miss Melody 4DWave PCI},"
		"{HIS,4DWave PCI},"
		"{Warpspeed,ONSpeed 4DWave PCI},"
		"{Aztech Systems,PCI 64-Q3D},"
		"{Addonics,SV 750},"
		"{CHIC,True Sound 4Dwave},"
		"{Shark,Predator4D-PCI},"
		"{Jaton,SonicWave 4D},"
		"{Hoontech,SoundTrack Digital 4DWave NX}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
#ifdef TARGET_OS2
static int snd_pcm_channels[SNDRV_CARDS] = {REPEAT_SNDRV(32)};
static int snd_wavetable_size[SNDRV_CARDS] = {REPEAT_SNDRV(8192)};
#else
static int snd_pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 32};
static int snd_wavetable_size[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 8192};
#endif

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for Trident 4DWave PCI soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for Trident 4DWave PCI soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable Trident 4DWave PCI soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_pcm_channels, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_pcm_channels, "Number of hardware channels assigned for PCM.");
MODULE_PARM_SYNTAX(snd_pcm_channels, SNDRV_ENABLED ",default:32,allows:{{1,32}}");
MODULE_PARM(snd_wavetable_size, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_wavetable_size, "Maximum memory size in kB for wavetable synth.");
MODULE_PARM_SYNTAX(snd_wavetable_size, SNDRV_ENABLED ",default:8192,skill:advanced");

static struct pci_device_id snd_trident_ids[] __devinitdata = {
	{ 0x1023, 0x2000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* Trident 4DWave DX PCI Audio */
	{ 0x1023, 0x2001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* Trident 4DWave NX PCI Audio */
	{ 0x1039, 0x7018, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* SiS SI7018 PCI Audio */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_trident_ids);

static int __init snd_trident_probe(struct pci_dev *pci,
				    const struct pci_device_id *id)
{
	static int dev = 0;
	snd_card_t *card;
	trident_t *trident;
	const char *str;
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

	if ((err = snd_trident_create(card, pci,
				      snd_pcm_channels[dev],
				      2,
				      snd_wavetable_size[dev],
				      &trident)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_trident_pcm(trident, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:
	case TRIDENT_DEVICE_ID_NX:
		if ((err = snd_trident_foldback_pcm(trident, 1, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
		break;
	}
	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		if ((err = snd_trident_spdif_pcm(trident, 2, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_TRID4DWAVE,
				       trident->midi_port, 1,
				       trident->irq, 0, &trident->rmidi)) < 0) {
		snd_card_free(card);
		return err;
	}

#ifdef CONFIG_SND_SEQUENCER
	if ((err = snd_trident_attach_synthesizer(trident)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif

	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:
		str = "TRID4DWAVEDX";
		break;
	case TRIDENT_DEVICE_ID_NX:
		str = "TRID4DWAVENX";
		break;
	case TRIDENT_DEVICE_ID_SI7018:
		str = "SI7018";
		break;
	default:
		str = "Unknown";
	}
	strcpy(card->driver, str);
	if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		strcpy(card->shortname, "SiS ");
	} else {
		strcpy(card->shortname, "Trident ");
	}
	strcat(card->shortname, card->driver);
	sprintf(card->longname, "%s PCI Audio at 0x%lx, irq %d",
		card->shortname, trident->port, trident->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	PCI_SET_DRIVER_DATA(pci, card);
	dev++;
	return 0;
}

static void __exit snd_trident_remove(struct pci_dev *pci)
{
	snd_card_free(PCI_GET_DRIVER_DATA(pci));
	PCI_SET_DRIVER_DATA(pci, NULL);
}

#ifdef TARGET_OS2
static struct pci_driver driver = {
	0, 0,"Trident4DWaveAudio",
	snd_trident_ids,
	snd_trident_probe,
	snd_trident_remove,0,0
};                                
#else
static struct pci_driver driver = {
	name: "Trident4DWaveAudio",
	id_table: snd_trident_ids,
	probe: snd_trident_probe,
	remove: snd_trident_remove,
};                                
#endif

static int __init alsa_card_trident_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		snd_printk("Trident 4DWave PCI soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_trident_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_trident_init)
module_exit(alsa_card_trident_exit)

#ifndef MODULE

/* format is: snd-card-trident=snd_enable,snd_index,snd_id,
			       snd_pcm_channels,snd_wavetable_size */

static int __init alsa_card_trident_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,&snd_pcm_channels[nr_dev]) == 2 &&
	       get_option(&str,&snd_wavetable_size[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-card-trident=", alsa_card_trident_setup);

#endif /* ifndef MODULE */
