/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
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
  - spdif nonaudio consumer mode does not work (at least with my 
    Sony STR-DB830)
*/

#define SNDRV_MAIN_OBJECT_FILE

#include <sound/driver.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/mpu401.h>
#include <sound/info.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("ICEnsemble ICE1712 (Envy24)");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Hoontech SoundTrack DSP 24},"
		"{MidiMan M Audio,Delta 1010},"
		"{MidiMan M Audio,Delta DiO 2496},"
		"{MidiMan M Audio,Delta 66},"
		"{MidiMan M Audio,Delta 44},"
		"{MidiMan M Audio,Audiophile 24/96},"
		"{TerraTec,EWX 24/96},"
		"{TerraTec,EWS 88MT},"
		"{TerraTec,EWS 88D},"
		"{ICEnsemble,Generic ICE1712},"
		"{ICEnsemble,Generic Envy24}}");
MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for ICE1712 soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for ICE1712 soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable ICE1712 soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);

#ifndef PCI_VENDOR_ID_ICE
#define PCI_VENDOR_ID_ICE		0x1412
#endif
#ifndef PCI_DEVICE_ID_ICE_1712
#define PCI_DEVICE_ID_ICE_1712		0x1712
#endif

#define ICE1712_SUBDEVICE_STDSP24	0x12141217	/* Hoontech SoundTrack Audio DSP 24 */
#define ICE1712_SUBDEVICE_DELTA1010	0x121430d6
#define ICE1712_SUBDEVICE_DELTADIO2496	0x121431d6
#define ICE1712_SUBDEVICE_DELTA66	0x121432d6
#define ICE1712_SUBDEVICE_DELTA44	0x121433d6
#define ICE1712_SUBDEVICE_AUDIOPHILE	0x121434d6
#define ICE1712_SUBDEVICE_EWX2496	0x3b153011
#define ICE1712_SUBDEVICE_EWS88MT	0x3b151511
#define ICE1712_SUBDEVICE_EWS88D	0x3b152b11

/*
 *  Direct registers
 */

#define ICEREG(ice, x) ((ice)->port + ICE1712_REG_##x)

#define ICE1712_REG_CONTROL		0x00	/* byte */
#define   ICE1712_RESET			0x80	/* reset whole chip */
#define   ICE1712_SERR_LEVEL		0x04	/* SERR# level otherwise edge */
#define   ICE1712_NATIVE		0x01	/* native mode otherwise SB */
#define ICE1712_REG_IRQMASK		0x01	/* byte */
#define   ICE1712_IRQ_MPU1		0x80
#define   ICE1712_IRQ_TIMER		0x40
#define   ICE1712_IRQ_MPU2		0x20
#define   ICE1712_IRQ_PROPCM		0x10
#define   ICE1712_IRQ_FM		0x08	/* FM/MIDI - legacy */
#define   ICE1712_IRQ_PBKDS		0x04	/* playback DS channels */
#define   ICE1712_IRQ_CONCAP		0x02	/* consumer capture */
#define   ICE1712_IRQ_CONPBK		0x01	/* consumer playback */
#define ICE1712_REG_IRQSTAT		0x02	/* byte */
/* look to ICE1712_IRQ_* */
#define ICE1712_REG_INDEX		0x03	/* byte - indirect CCIxx regs */
#define ICE1712_REG_DATA		0x04	/* byte - indirect CCIxx regs */
#define ICE1712_REG_NMI_STAT1		0x05	/* byte */
#define ICE1712_REG_NMI_DATA		0x06	/* byte */
#define ICE1712_REG_NMI_INDEX		0x07	/* byte */
#define ICE1712_REG_AC97_INDEX		0x08	/* byte */
#define ICE1712_REG_AC97_CMD		0x09	/* byte */
#define   ICE1712_AC97_COLD		0x80	/* cold reset */
#define   ICE1712_AC97_WARM		0x40	/* warm reset */
#define   ICE1712_AC97_WRITE		0x20	/* W: write, R: write in progress */
#define   ICE1712_AC97_READ		0x10	/* W: read, R: read in progress */
#define   ICE1712_AC97_READY		0x08	/* codec ready status bit */
#define   ICE1712_AC97_PBK_VSR		0x02	/* playback VSR */
#define   ICE1712_AC97_CAP_VSR		0x01	/* capture VSR */
#define ICE1712_REG_AC97_DATA		0x0a	/* word (little endian) */
#define ICE1712_REG_MPU1_CTRL		0x0c	/* byte */
#define ICE1712_REG_MPU1_DATA		0x0d	/* byte */
#define ICE1712_REG_I2C_DEV_ADDR	0x10	/* byte */
#define   ICE1712_I2C_WRITE		0x01	/* write direction */
#define ICE1712_REG_I2C_BYTE_ADDR	0x11	/* byte */
#define ICE1712_REG_I2C_DATA		0x12	/* byte */
#define ICE1712_REG_I2C_CTRL		0x13	/* byte */
#define   ICE1712_I2C_EEPROM		0x80	/* EEPROM exists */
#define   ICE1712_I2C_BUSY		0x01	/* busy bit */
#define ICE1712_REG_CONCAP_ADDR		0x14	/* dword - consumer capture */
#define ICE1712_REG_CONCAP_COUNT	0x18	/* word - current/base count */
#define ICE1712_REG_SERR_SHADOW		0x1b	/* byte */
#define ICE1712_REG_MPU2_CTRL		0x1c	/* byte */
#define ICE1712_REG_MPU2_DATA		0x1d	/* byte */
#define ICE1712_REG_TIMER		0x1e	/* word */

/*
 *  Indirect registers
 */

#define ICE1712_IREG_PBK_COUNT_HI	0x00
#define ICE1712_IREG_PBK_COUNT_LO	0x01
#define ICE1712_IREG_PBK_CTRL		0x02
#define ICE1712_IREG_PBK_LEFT		0x03	/* left volume */
#define ICE1712_IREG_PBK_RIGHT		0x04	/* right volume */
#define ICE1712_IREG_PBK_SOFT		0x05	/* soft volume */
#define ICE1712_IREG_PBK_RATE_LO	0x06
#define ICE1712_IREG_PBK_RATE_MID	0x07
#define ICE1712_IREG_PBK_RATE_HI	0x08
#define ICE1712_IREG_CAP_COUNT_HI	0x10
#define ICE1712_IREG_CAP_COUNT_LO	0x11
#define ICE1712_IREG_CAP_CTRL		0x12
#define ICE1712_IREG_GPIO_DATA		0x20
#define ICE1712_IREG_GPIO_WRITE_MASK	0x21
#define ICE1712_IREG_GPIO_DIRECTION	0x22
#define ICE1712_IREG_CONSUMER_POWERDOWN	0x30
#define ICE1712_IREG_PRO_POWERDOWN	0x31

/*
 *  Consumer section direct DMA registers
 */

#define ICEDS(ice, x) ((ice)->dmapath_port + ICE1712_DS_##x)
 
#define ICE1712_DS_INTMASK		0x00	/* word - interrupt mask */
#define ICE1712_DS_INTSTAT		0x02	/* word - interrupt status */
#define ICE1712_DS_DATA			0x04	/* dword - channel data */
#define ICE1712_DS_INDEX		0x08	/* dword - channel index */

/*
 *  Consumer section channel registers
 */
 
#define ICE1712_DSC_ADDR0		0x00	/* dword - base address 0 */
#define ICE1712_DSC_COUNT0		0x01	/* word - count 0 */
#define ICE1712_DSC_ADDR1		0x02	/* dword - base address 1 */
#define ICE1712_DSC_COUNT1		0x03	/* word - count 1 */
#define ICE1712_DSC_CONTROL		0x04	/* byte - control & status */
#define   ICE1712_BUFFER1		0x80	/* buffer1 is active */
#define   ICE1712_BUFFER1_AUTO		0x40	/* buffer1 auto init */
#define   ICE1712_BUFFER0_AUTO		0x20	/* buffer0 auto init */
#define   ICE1712_FLUSH			0x10	/* flush FIFO */
#define   ICE1712_STEREO		0x08	/* stereo */
#define   ICE1712_16BIT			0x04	/* 16-bit data */
#define   ICE1712_PAUSE			0x02	/* pause */
#define   ICE1712_START			0x01	/* start */
#define ICE1712_DSC_RATE		0x05	/* dword - rate */
#define ICE1712_DSC_VOLUME		0x06	/* word - volume control */

/* 
 *  Professional multi-track direct control registers
 */

#define ICEMT(ice, x) ((ice)->profi_port + ICE1712_MT_##x)

#define ICE1712_MT_IRQ			0x00	/* byte - interrupt mask */
#define   ICE1712_MULTI_CAPTURE		0x80	/* capture IRQ */
#define   ICE1712_MULTI_PLAYBACK	0x40	/* playback IRQ */
#define   ICE1712_MULTI_CAPSTATUS	0x02	/* capture IRQ status */
#define   ICE1712_MULTI_PBKSTATUS	0x01	/* playback IRQ status */
#define ICE1712_MT_RATE			0x01	/* byte - sampling rate select */
#define   ICE1712_SPDIF_MASTER		0x10	/* S/PDIF input is master clock */
#define ICE1712_MT_I2S_FORMAT		0x02	/* byte - I2S data format */
#define ICE1712_MT_AC97_INDEX		0x04	/* byte - AC'97 index */
#define ICE1712_MT_AC97_CMD		0x05	/* byte - AC'97 command & status */
/* look to ICE1712_AC97_* */
#define ICE1712_MT_AC97_DATA		0x06	/* word - AC'97 data */
#define ICE1712_MT_PLAYBACK_ADDR	0x10	/* dword - playback address */
#define ICE1712_MT_PLAYBACK_SIZE	0x14	/* word - playback size */
#define ICE1712_MT_PLAYBACK_COUNT	0x16	/* word - playback count */
#define ICE1712_MT_PLAYBACK_CONTROL	0x18	/* byte - control */
#define   ICE1712_CAPTURE_START_SHADOW	0x04	/* capture start */
#define   ICE1712_PLAYBACK_PAUSE	0x02	/* playback pause */
#define   ICE1712_PLAYBACK_START	0x01	/* playback start */
#define ICE1712_MT_CAPTURE_ADDR		0x20	/* dword - capture address */
#define ICE1712_MT_CAPTURE_SIZE		0x24	/* word - capture size */
#define ICE1712_MT_CAPTURE_COUNT	0x26	/* word - capture count */
#define ICE1712_MT_CAPTURE_CONTROL	0x28	/* byte - control */
#define   ICE1712_CAPTURE_START		0x01	/* capture start */
#define ICE1712_MT_ROUTE_PSDOUT03	0x30	/* word */
#define ICE1712_MT_ROUTE_SPDOUT		0x32	/* word */
#define ICE1712_MT_ROUTE_CAPTURE	0x34	/* dword */
#define ICE1712_MT_MONITOR_VOLUME	0x38	/* word */
#define ICE1712_MT_MONITOR_INDEX	0x3a	/* byte */
#define ICE1712_MT_MONITOR_RATE		0x3b	/* byte */
#define ICE1712_MT_MONITOR_ROUTECTRL	0x3c	/* byte */
#define   ICE1712_ROUTE_AC97		0x01	/* route digital mixer output to AC'97 */
#define ICE1712_MT_MONITOR_PEAKINDEX	0x3e	/* byte */
#define ICE1712_MT_MONITOR_PEAKDATA	0x3f	/* byte */

/*
 *  Codec configuration bits
 */

/* PCI[60] System Configuration */
#define ICE1712_CFG_CLOCK	0xc0
#define   ICE1712_CFG_CLOCK512	0x00	/* 22.5692Mhz, 44.1kHz*512 */
#define   ICE1712_CFG_CLOCK384  0x40	/* 16.9344Mhz, 44.1kHz*384 */
#define   ICE1712_CFG_EXT	0x80	/* external clock */
#define ICE1712_CFG_2xMPU401	0x20	/* two MPU401 UARTs */
#define ICE1712_CFG_NO_CON_AC97 0x10	/* consumer AC'97 codec is not present */
#define ICE1712_CFG_ADC_MASK	0x0c	/* one, two, three, four stereo ADCs */
#define ICE1712_CFG_DAC_MASK	0x03	/* one, two, three, four stereo DACs */
/* PCI[61] AC-Link Configuration */
#define ICE1712_CFG_PRO_I2S	0x80	/* multitrack converter: I2S or AC'97 */
#define ICE1712_CFG_AC97_PACKED	0x01	/* split or packed mode - AC'97 */
/* PCI[62] I2S Features */
#define ICE1712_CFG_I2S_VOLUME	0x80	/* volume/mute capability */
#define ICE1712_CFG_I2S_96KHZ	0x40	/* supports 96kHz sampling */
#define ICE1712_CFG_I2S_RESMASK	0x30	/* resolution mask, 16,18,20,24-bit */
#define ICE1712_CFG_I2S_OTHER	0x0f	/* other I2S IDs */
/* PCI[63] S/PDIF Configuration */
#define ICE1712_CFG_I2S_CHIPID	0xfc	/* I2S chip ID */
#define ICE1712_CFG_SPDIF_IN	0x02	/* S/PDIF input is present */
#define ICE1712_CFG_SPDIF_OUT	0x01	/* S/PDIF output is present */

/* MidiMan Delta GPIO definitions */

#define ICE1712_DELTA_DFS	0x01	/* fast/slow sample rate mode */
					/* (>48kHz must be 1) */
					/* all cards */
#define ICE1712_DELTA_SPDIF_IN_STAT 0x02
					/* S/PDIF input status */
					/* 0 = valid signal is present */
					/* all except Delta44 */
					/* look to CS8414 datasheet */
#define ICE1712_DELTA_SPDIF_OUT_STAT_CLOCK 0x04
					/* S/PDIF output status clock */
					/* (writting on rising edge - 0->1) */
					/* all except Delta44 */
					/* look to CS8404A datasheet */
#define ICE1712_DELTA_SPDIF_OUT_STAT_DATA 0x08
					/* S/PDIF output status data */
					/* all except Delta44 */
					/* look to CS8404A datasheet */
#define ICE1712_DELTA_SPDIF_INPUT_SELECT 0x10
					/* coaxial (0), optical (1) */
					/* S/PDIF input select*/
					/* DeltaDiO only */
#define ICE1712_DELTA_WORD_CLOCK_SELECT 0x10
					/* 1 - clock are taken from S/PDIF input */
					/* 0 - clock are taken from Word Clock input */
					/* Delta1010 only (affected SPMCLKIN pin of Envy24) */
#define ICE1712_DELTA_CODEC_SERIAL_DATA 0x10
					/* AKM4524 serial data */
					/* Delta66 and Delta44 */
#define ICE1712_DELTA_WORD_CLOCK_STATUS	0x20
					/* 0 = valid word clock signal is present */
					/* Delta1010 only */
#define ICE1712_DELTA_CODEC_SERIAL_CLOCK 0x20
					/* AKM4524 serial clock */
					/* (writting on rising edge - 0->1 */
					/* Delta66 and Delta44 */
#define ICE1712_DELTA_CODEC_CHIP_A	0x40
#define ICE1712_DELTA_CODEC_CHIP_B	0x80
					/* 1 - select chip A or B */
					/* Delta66 and Delta44 */

/* M-Audio Audiophile definitions */
/* 0x01 = DFS */
#define ICE1712_DELTA_AP_CCLK	0x02	/* AudioPhile only */
					/* SPI clock */
					/* (clocking on rising edge - 0->1) */
#define ICE1712_DELTA_AP_DIN	0x04	/* AudioPhile only - data input */
#define ICE1712_DELTA_AP_DOUT	0x08	/* AudioPhile only - data output */
#define ICE1712_DELTA_AP_CS_DIGITAL 0x10 /* AudioPhile only - CS8427 chip select */
					/* low signal = select */
#define ICE1712_DELTA_AP_CS_CODEC 0x20	/* AudioPhile only - AK4528 chip select */
					/* low signal = select */

/* Hoontech SoundTrack Audio DSP 24 GPIO definitions */

#define ICE1712_STDSP24_0_BOX(r, x)	r[0] = ((r[0] & ~3) | ((x)&3))
#define ICE1712_STDSP24_0_DAREAR(r, x)	r[0] = ((r[0] & ~4) | (((x)&1)<<2))
#define ICE1712_STDSP24_1_CHN1(r, x)	r[1] = ((r[1] & ~1) | ((x)&1))
#define ICE1712_STDSP24_1_CHN2(r, x)	r[1] = ((r[1] & ~2) | (((x)&1)<<1))
#define ICE1712_STDSP24_1_CHN3(r, x)	r[1] = ((r[1] & ~4) | (((x)&1)<<2))
#define ICE1712_STDSP24_2_CHN4(r, x)	r[2] = ((r[2] & ~1) | ((x)&1))
#define ICE1712_STDSP24_2_MIDIIN(r, x)	r[2] = ((r[2] & ~2) | (((x)&1)<<1))
#define ICE1712_STDSP24_2_MIDI1(r, x)	r[2] = ((r[2] & ~4) | (((x)&1)<<2))
#define ICE1712_STDSP24_3_MIDI2(r, x)	r[3] = ((r[3] & ~1) | ((x)&1))
#define ICE1712_STDSP24_3_MUTE(r, x)	r[3] = ((r[3] & ~2) | (((x)&1)<<1))
#define ICE1712_STDSP24_3_INSEL(r, x)	r[3] = ((r[3] & ~4) | (((x)&1)<<2))
#define ICE1712_STDSP24_SET_ADDR(r, a)	r[a&3] = ((r[a&3] & ~0x18) | (((a)&3)<<3))
#define ICE1712_STDSP24_CLOCK(r, a, c)	r[a&3] = ((r[a&3] & ~0x20) | (((c)&1)<<5))
#define ICE1712_STDSP24_CLOCK_BIT	(1<<5)

/* Hoontech SoundTrack Audio DSP 24 box configuration definitions */

#define ICE1712_STDSP24_DAREAR		(1<<0)
#define ICE1712_STDSP24_MUTE		(1<<1)
#define ICE1712_STDSP24_INSEL		(1<<2)

#define ICE1712_STDSP24_BOX_CHN1	(1<<0)	/* input channel 1 */
#define ICE1712_STDSP24_BOX_CHN2	(1<<1)	/* input channel 2 */
#define ICE1712_STDSP24_BOX_CHN3	(1<<2)	/* input channel 3 */
#define ICE1712_STDSP24_BOX_CHN4	(1<<3)	/* input channel 4 */
#define ICE1712_STDSP24_BOX_MIDI1	(1<<8)
#define ICE1712_STDSP24_BOX_MIDI2	(1<<9)

/* TerraTec EWX 24/96 configuration definitions */

#define ICE1712_EWX2496_AK4524_CS	0x01	/* AK4524 chip select; low = active */
#define ICE1712_EWX2496_AIN_SEL		0x02	/* input sensitivity switch; high = louder */
#define ICE1712_EWX2496_AOUT_SEL	0x04	/* output sensitivity switch; high = louder */
#define ICE1712_EWX2496_RW		0x08	/* read/write switch for i2c; high = write  */
#define ICE1712_EWX2496_SERIAL_DATA	0x10	/* i2c & ak4524 data */
#define ICE1712_EWX2496_SERIAL_CLOCK	0x20	/* i2c & ak4524 clock */
#define ICE1712_EWX2496_TX2		0x40	/* MIDI2 (not used) */
#define ICE1712_EWX2496_RX2		0x80	/* MIDI2 (not used) */

/* TerraTec EWS 88MT/D configuration definitions */
/* RW, SDA snd SCLK are identical with EWX24/96 */

#define ICE1712_EWS88_CS8414_RATE	0x07	/* CS8414 sample rate: gpio 0-2 */
#define ICE1712_EWS88_RW		0x08	/* read/write switch for i2c; high = write  */
#define ICE1712_EWS88_SERIAL_DATA	0x10	/* i2c & ak4524 data */
#define ICE1712_EWS88_SERIAL_CLOCK	0x20	/* i2c & ak4524 clock */
#define ICE1712_EWS88_TX2		0x40	/* MIDI2 (only on 88D) */
#define ICE1712_EWS88_RX2		0x80	/* MIDI2 (only on 88D) */

/* i2c address */
#define ICE1712_EWS88MT_CS8404_ADDR	0x40
#define ICE1712_EWS88MT_INPUT_ADDR	0x46
#define ICE1712_EWS88MT_OUTPUT_ADDR	0x48
#define ICE1712_EWS88MT_OUTPUT_SENSE	0x40	/* mask */
#define ICE1712_EWS88D_PCF_ADDR		0x40

/*
 * DMA mode values
 * identical with DMA_XXX on i386 architecture.
 */
#define ICE1712_DMA_MODE_WRITE		0x48
#define ICE1712_DMA_AUTOINIT		0x10


/*
 *  
 */

typedef struct _snd_ice1712 ice1712_t;

typedef struct {
	void (*write)(ice1712_t *ice, unsigned char reg, unsigned char data);
	unsigned char (*read)(ice1712_t *ice, unsigned char reg);
	void (*write_bytes)(ice1712_t *ice, unsigned char reg, int bytes, unsigned char *data);
	void (*read_bytes)(ice1712_t *ice, unsigned char reg, int bytes, unsigned char *data);
} ice1712_cs8427_ops_t;

typedef struct {
	unsigned int subvendor;	/* PCI[2c-2f] */
	unsigned char size;	/* size of EEPROM image in bytes */
	unsigned char version;	/* must be 1 */
	unsigned char codec;	/* codec configuration PCI[60] */
	unsigned char aclink;	/* ACLink configuration PCI[61] */
	unsigned char i2sID;	/* PCI[62] */
	unsigned char spdif;	/* S/PDIF configuration PCI[63] */
	unsigned char gpiomask;	/* GPIO initial mask, 0 = write, 1 = don't */
	unsigned char gpiostate; /* GPIO initial state */
	unsigned char gpiodir;	/* GPIO direction state */
	unsigned short ac97main;
	unsigned short ac97pcm;
	unsigned short ac97rec;
	unsigned char ac97recsrc;
	unsigned char dacID[4];	/* I2S IDs for DACs */
	unsigned char adcID[4];	/* I2S IDs for ADCs */
	unsigned char extra[4];
} ice1712_eeprom_t;

struct _snd_ice1712 {
	unsigned long conp_dma_size;
	unsigned long conc_dma_size;
	unsigned long prop_dma_size;
	unsigned long proc_dma_size;
	int irq;

	unsigned long port;
	struct resource *res_port;
	unsigned long ddma_port;
	struct resource *res_ddma_port;
	unsigned long dmapath_port;
	struct resource *res_dmapath_port;
	unsigned long profi_port;
	struct resource *res_profi_port;

	unsigned int config;	/* system configuration */

	struct pci_dev *pci;
	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_pcm_t *pcm_ds;
	snd_pcm_t *pcm_pro;
        snd_pcm_substream_t *playback_con_substream;
        snd_pcm_substream_t *playback_con_substream_ds[6];
        snd_pcm_substream_t *capture_con_substream;
        snd_pcm_substream_t *playback_pro_substream;
        snd_pcm_substream_t *capture_pro_substream;
	unsigned int playback_pro_size;
	unsigned int capture_pro_size;
	unsigned int playback_con_virt_addr[6];
	unsigned int playback_con_active_buf[6];
	unsigned int capture_con_virt_addr;
	unsigned int ac97_ext_id;
	ac97_t *ac97;
	snd_rawmidi_t *rmidi[2];

	spinlock_t reg_lock;
	struct semaphore gpio_mutex;
	snd_info_entry_t *proc_entry;

	ice1712_eeprom_t eeprom;

	unsigned int pro_volumes[20];
	int num_adcs;
	int num_dacs;
	unsigned char ak4524_images[4][8];
	unsigned char hoontech_boxbits[4];
	unsigned int hoontech_config;
	unsigned short hoontech_boxconfig[4];

	unsigned char cs8403_spdif_bits;
	unsigned char cs8403_spdif_stream_bits;
	snd_kcontrol_t *spdif_stream_ctl;

	unsigned char cs8427_spdif_status[5];
	unsigned char cs8427_spdif_stream_status[5];
	ice1712_cs8427_ops_t *cs8427_ops;

	unsigned char gpio_direction, gpio_write_mask;
};

#define chip_t ice1712_t

static struct pci_device_id snd_ice1712_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_ICE, PCI_DEVICE_ID_ICE_1712, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* ICE1712 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_ice1712_ids);

static int snd_ice1712_build_pro_mixer(ice1712_t *ice);
static int snd_ice1712_build_controls(ice1712_t *ice);

/*
 *  Basic I/O
 */
 
static inline void snd_ice1712_write(ice1712_t * ice, u8 addr, u8 data)
{
	outb(addr, ICEREG(ice, INDEX));
	outb(data, ICEREG(ice, DATA));
}

static inline u8 snd_ice1712_read(ice1712_t * ice, u8 addr)
{
	outb(addr, ICEREG(ice, INDEX));
	return inb(ICEREG(ice, DATA));
}

static inline void snd_ice1712_ds_write(ice1712_t * ice, u8 channel, u8 addr, u32 data)
{
	outb((channel << 4) | addr, ICEDS(ice, INDEX));
	outl(data, ICEDS(ice, DATA));
}

static inline u32 snd_ice1712_ds_read(ice1712_t * ice, u8 channel, u8 addr)
{
	outb((channel << 4) | addr, ICEDS(ice, INDEX));
	return inl(ICEDS(ice, DATA));
}

static void snd_ice1712_ac97_write(ac97_t *ac97,
				   unsigned short reg,
				   unsigned short val)
{
	ice1712_t *ice = (ice1712_t *)ac97->private_data;
	int tm;
	unsigned char old_cmd = 0;

	for (tm = 0; tm < 0x10000; tm++) {
		old_cmd = inb(ICEREG(ice, AC97_CMD));
		if (old_cmd & (ICE1712_AC97_WRITE | ICE1712_AC97_READ))
			continue;
		if (!(old_cmd & ICE1712_AC97_READY))
			continue;
		break;
	}
	outb(reg, ICEREG(ice, AC97_INDEX));
	outw(val, ICEREG(ice, AC97_DATA));
	old_cmd &= ~(ICE1712_AC97_PBK_VSR | ICE1712_AC97_CAP_VSR);
	outb(old_cmd | ICE1712_AC97_WRITE, ICEREG(ice, AC97_CMD));
	for (tm = 0; tm < 0x10000; tm++)
		if ((inb(ICEREG(ice, AC97_CMD)) & ICE1712_AC97_WRITE) == 0)
			break;
}

static unsigned short snd_ice1712_ac97_read(ac97_t *ac97,
					    unsigned short reg)
{
	ice1712_t *ice = (ice1712_t *)ac97->private_data;
	int tm;
	unsigned char old_cmd = 0;

	for (tm = 0; tm < 0x10000; tm++) {
		old_cmd = inb(ICEREG(ice, AC97_CMD));
		if (old_cmd & (ICE1712_AC97_WRITE | ICE1712_AC97_READ))
			continue;
		if (!(old_cmd & ICE1712_AC97_READY))
			continue;
		break;
	}
	outb(reg, ICEREG(ice, AC97_INDEX));
	outb(old_cmd | ICE1712_AC97_READ, ICEREG(ice, AC97_CMD));
	for (tm = 0; tm < 0x10000; tm++)
		if ((inb(ICEREG(ice, AC97_CMD)) & ICE1712_AC97_READ) == 0)
			break;
	if (tm >= 0x10000)		/* timeout */
		return ~0;
	return inw(ICEREG(ice, AC97_DATA));
}

/*
 * pro ac97 section
 */

static void snd_ice1712_pro_ac97_write(ac97_t *ac97,
				       unsigned short reg,
				       unsigned short val)
{
	ice1712_t *ice = (ice1712_t *)ac97->private_data;
	int tm;
	unsigned char old_cmd = 0;

	for (tm = 0; tm < 0x10000; tm++) {
		old_cmd = inb(ICEMT(ice, AC97_CMD));
		if (old_cmd & (ICE1712_AC97_WRITE | ICE1712_AC97_READ))
			continue;
		if (!(old_cmd & ICE1712_AC97_READY))
			continue;
		break;
	}
	outb(reg, ICEMT(ice, AC97_INDEX));
	outw(val, ICEMT(ice, AC97_DATA));
	old_cmd &= ~(ICE1712_AC97_PBK_VSR | ICE1712_AC97_CAP_VSR);
	outb(old_cmd | ICE1712_AC97_WRITE, ICEMT(ice, AC97_CMD));
	for (tm = 0; tm < 0x10000; tm++)
		if ((inb(ICEMT(ice, AC97_CMD)) & ICE1712_AC97_WRITE) == 0)
			break;
}


static unsigned short snd_ice1712_pro_ac97_read(ac97_t *ac97,
						unsigned short reg)
{
	ice1712_t *ice = (ice1712_t *)ac97->private_data;
	int tm;
	unsigned char old_cmd = 0;

	for (tm = 0; tm < 0x10000; tm++) {
		old_cmd = inb(ICEMT(ice, AC97_CMD));
		if (old_cmd & (ICE1712_AC97_WRITE | ICE1712_AC97_READ))
			continue;
		if (!(old_cmd & ICE1712_AC97_READY))
			continue;
		break;
	}
	outb(reg, ICEMT(ice, AC97_INDEX));
	outb(old_cmd | ICE1712_AC97_READ, ICEMT(ice, AC97_CMD));
	for (tm = 0; tm < 0x10000; tm++)
		if ((inb(ICEMT(ice, AC97_CMD)) & ICE1712_AC97_READ) == 0)
			break;
	if (tm >= 0x10000)		/* timeout */
		return ~0;
	return inw(ICEMT(ice, AC97_DATA));
}

static int snd_ice1712_digmix_route_ac97_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_digmix_route_ac97_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&ice->reg_lock, flags);
	ucontrol->value.integer.value[0] = inb(ICEMT(ice, MONITOR_ROUTECTRL)) & ICE1712_ROUTE_AC97 ? 1 : 0;
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static int snd_ice1712_digmix_route_ac97_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val, nval;
	unsigned long flags;
	
	spin_lock_irqsave(&ice->reg_lock, flags);
	val = inb(ICEMT(ice, MONITOR_ROUTECTRL));
	nval = val & ~ICE1712_ROUTE_AC97;
	if (ucontrol->value.integer.value[0]) nval |= ICE1712_ROUTE_AC97;
	outb(nval, ICEMT(ice, MONITOR_ROUTECTRL));
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return val != nval;
}

static snd_kcontrol_new_t snd_ice1712_mixer_digmix_route_ac97 = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Digital Mixer To AC97",0,0,
	snd_ice1712_digmix_route_ac97_info,
	snd_ice1712_digmix_route_ac97_get,
	snd_ice1712_digmix_route_ac97_put,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Digital Mixer To AC97",
	info: snd_ice1712_digmix_route_ac97_info,
	get: snd_ice1712_digmix_route_ac97_get,
	put: snd_ice1712_digmix_route_ac97_put,
#endif
};


/*
 */
static void snd_ice1712_delta_cs8403_spdif_write(ice1712_t *ice, unsigned char bits)
{
	unsigned char tmp, mask1, mask2;
	int idx;
	/* send byte to transmitter */
	mask1 = ICE1712_DELTA_SPDIF_OUT_STAT_CLOCK;
	mask2 = ICE1712_DELTA_SPDIF_OUT_STAT_DATA;
	down(&ice->gpio_mutex);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	for (idx = 7; idx >= 0; idx--) {
		tmp &= ~(mask1 | mask2);
		if (bits & (1 << idx))
			tmp |= mask2;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(100);
		tmp |= mask1;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(100);
	}
	tmp &= ~mask1;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	up(&ice->gpio_mutex);
}

static void snd_ice1712_decode_cs8403_spdif_bits(snd_aes_iec958_t *diga,
						 unsigned char bits)
{
	if (bits & 0x01) {	/* consumer */
		if (!(bits & 0x02))
			diga->status[0] |= IEC958_AES0_NONAUDIO;
		if (!(bits & 0x08))
			diga->status[0] |= IEC958_AES0_CON_NOT_COPYRIGHT;
		switch (bits & 0x10) {
		case 0x10: diga->status[0] |= IEC958_AES0_CON_EMPHASIS_NONE; break;
		case 0x00: diga->status[0] |= IEC958_AES0_CON_EMPHASIS_5015; break;
		}
		if (!(bits & 0x80))
			diga->status[1] |= IEC958_AES1_CON_ORIGINAL;
		switch (bits & 0x60) {
		case 0x00: diga->status[1] |= IEC958_AES1_CON_MAGNETIC_ID; break;
		case 0x20: diga->status[1] |= IEC958_AES1_CON_DIGDIGCONV_ID; break;
		case 0x40: diga->status[1] |= IEC958_AES1_CON_LASEROPT_ID; break;
		case 0x60: diga->status[1] |= IEC958_AES1_CON_GENERAL; break;
		}
		switch (bits & 0x06) {
		case 0x00: diga->status[3] |= IEC958_AES3_CON_FS_44100; break;
		case 0x02: diga->status[3] |= IEC958_AES3_CON_FS_48000; break;
		case 0x04: diga->status[3] |= IEC958_AES3_CON_FS_32000; break;
		}
	} else {
		diga->status[0] = IEC958_AES0_PROFESSIONAL;
		switch (bits & 0x18) {
		case 0x00: diga->status[0] |= IEC958_AES0_PRO_FS_32000; break;
		case 0x10: diga->status[0] |= IEC958_AES0_PRO_FS_44100; break;
		case 0x08: diga->status[0] |= IEC958_AES0_PRO_FS_48000; break;
		case 0x18: diga->status[0] |= IEC958_AES0_PRO_FS_NOTID; break;
		}
		switch (bits & 0x60) {
		case 0x20: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_NONE; break;
		case 0x40: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_5015; break;
		case 0x00: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_CCITT; break;
		case 0x60: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_NOTID; break;
		}
		if (bits & 0x80)
			diga->status[1] |= IEC958_AES1_PRO_MODE_STEREOPHONIC;
	}
}

static unsigned char snd_ice1712_encode_cs8403_spdif_bits(snd_aes_iec958_t *diga)
{
	unsigned char bits;

	if (!(diga->status[0] & IEC958_AES0_PROFESSIONAL)) {
		bits = 0x01;	/* consumer mode */
		if (diga->status[0] & IEC958_AES0_NONAUDIO)
			bits &= ~0x02;
		else
			bits |= 0x02;
		if (diga->status[0] & IEC958_AES0_CON_NOT_COPYRIGHT)
			bits &= ~0x08;
		else
			bits |= 0x08;
		switch (diga->status[0] & IEC958_AES0_CON_EMPHASIS) {
		default:
		case IEC958_AES0_CON_EMPHASIS_NONE: bits |= 0x10; break;
		case IEC958_AES0_CON_EMPHASIS_5015: bits |= 0x00; break;
		}
		if (diga->status[1] & IEC958_AES1_CON_ORIGINAL)
			bits &= ~0x80;
		else
			bits |= 0x80;
		if ((diga->status[1] & IEC958_AES1_CON_CATEGORY) == IEC958_AES1_CON_GENERAL)
			bits |= 0x60;
		else {
			switch(diga->status[1] & IEC958_AES1_CON_MAGNETIC_MASK) {
			case IEC958_AES1_CON_MAGNETIC_ID:
				bits |= 0x00; break;
			case IEC958_AES1_CON_DIGDIGCONV_ID:
				bits |= 0x20; break;
			default:
			case IEC958_AES1_CON_LASEROPT_ID:
				bits |= 0x40; break;
			}
		}
		switch (diga->status[3] & IEC958_AES3_CON_FS) {
		default:
		case IEC958_AES3_CON_FS_44100: bits |= 0x00; break;
		case IEC958_AES3_CON_FS_48000: bits |= 0x02; break;
		case IEC958_AES3_CON_FS_32000: bits |= 0x04; break;
		}
	} else {
		bits = 0x00;	/* professional mode */
		if (diga->status[0] & IEC958_AES0_NONAUDIO)
			bits &= ~0x02;
		else
			bits |= 0x02;
		/* CHECKME: I'm not sure about the bit order in val here */
		switch (diga->status[0] & IEC958_AES0_PRO_FS) {
		case IEC958_AES0_PRO_FS_32000:	bits |= 0x00; break;
		case IEC958_AES0_PRO_FS_44100:	bits |= 0x10; break;	/* 44.1kHz */
		case IEC958_AES0_PRO_FS_48000:	bits |= 0x08; break;	/* 48kHz */
		default:
		case IEC958_AES0_PRO_FS_NOTID: bits |= 0x18; break;
		}
		switch (diga->status[0] & IEC958_AES0_PRO_EMPHASIS) {
		case IEC958_AES0_PRO_EMPHASIS_NONE: bits |= 0x20; break;
		case IEC958_AES0_PRO_EMPHASIS_5015: bits |= 0x40; break;
		case IEC958_AES0_PRO_EMPHASIS_CCITT: bits |= 0x00; break;
		default:
		case IEC958_AES0_PRO_EMPHASIS_NOTID: bits |= 0x60; break;
		}
		switch (diga->status[1] & IEC958_AES1_PRO_MODE) {
		case IEC958_AES1_PRO_MODE_TWO:
		case IEC958_AES1_PRO_MODE_STEREOPHONIC: bits |= 0x00; break;
		default: bits |= 0x80; break;
		}
	}
	return bits;
}


/*
 * set gpio direction, write mask and data
 */
static void snd_ice1712_gpio_write_bits(ice1712_t *ice, int mask, int bits)
{
	ice->gpio_direction |= mask;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, ice->gpio_direction);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~mask);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, mask & bits);
}

/*
 */
static void save_gpio_status(ice1712_t *ice, unsigned char *tmp)
{
	down(&ice->gpio_mutex);
	tmp[0] = ice->gpio_direction;
	tmp[1] = ice->gpio_write_mask;
}

static void restore_gpio_status(ice1712_t *ice, unsigned char *tmp)
{
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, tmp[0]);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, tmp[1]);
	ice->gpio_direction = tmp[0];
	ice->gpio_write_mask = tmp[1];
	up(&ice->gpio_mutex);
}

/*
 * CS8427 via SPI mode (for Audiophile)
 */

/* send 8 bits */
static void ap_cs8427_write_byte(ice1712_t *ice, unsigned char data, unsigned char tmp)
{
	int idx;

	for (idx = 7; idx >= 0; idx--) {
		tmp &= ~(ICE1712_DELTA_AP_DOUT|ICE1712_DELTA_AP_CCLK);
		if (data & (1 << idx))
			tmp |= ICE1712_DELTA_AP_DOUT;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
		tmp |= ICE1712_DELTA_AP_CCLK;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
	}
}

/* read 8 bits */
static unsigned char ap_cs8427_read_byte(ice1712_t *ice, unsigned char tmp)
{
	unsigned char data = 0;
	int idx;
	
	for (idx = 7; idx >= 0; idx--) {
		tmp &= ~ICE1712_DELTA_AP_CCLK;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
		if (snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & ICE1712_DELTA_AP_DIN)
			data |= 1 << idx;
		tmp |= ICE1712_DELTA_AP_CCLK;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(5);
	}
	return data;
}

/* assert chip select */
static unsigned char ap_cs8427_codec_select(ice1712_t *ice)
{
	unsigned char tmp;
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	tmp |= ICE1712_DELTA_AP_CCLK | ICE1712_DELTA_AP_CS_CODEC;
	tmp &= ~ICE1712_DELTA_AP_CS_DIGITAL;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	udelay(5);
	return tmp;
}

/* deassert chip select */
static void ap_cs8427_codec_deassert(ice1712_t *ice, unsigned char tmp)
{
	tmp |= ICE1712_DELTA_AP_CS_DIGITAL;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
}

/* write a register */
static void snd_ice1712_ap_cs8427_write(ice1712_t *ice, unsigned char reg, unsigned char data)
{
	unsigned char tmp;
	down(&ice->gpio_mutex);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, 0x20, tmp); /* address + write mode */
	ap_cs8427_write_byte(ice, reg, tmp);
	ap_cs8427_write_byte(ice, data, tmp);
	ap_cs8427_codec_deassert(ice, tmp);
	up(&ice->gpio_mutex);
}

/* read a register */
static unsigned char snd_ice1712_ap_cs8427_read(ice1712_t *ice, unsigned char reg)
{
	unsigned char tmp, data;

	down(&ice->gpio_mutex);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, 0x20, tmp); /* address + write mode */
	ap_cs8427_write_byte(ice, reg, tmp);
	ap_cs8427_codec_deassert(ice, tmp);
	udelay(5);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, 0x21, tmp); /* address + read mode */
	data = ap_cs8427_read_byte(ice, tmp);
	ap_cs8427_codec_deassert(ice, tmp);
	up(&ice->gpio_mutex);
	return data;
}

/* sequential write */
static void snd_ice1712_ap_cs8427_write_bytes(ice1712_t *ice, unsigned char reg, int bytes, unsigned char *data)
{
#if 1
	unsigned char tmp;
	down(&ice->gpio_mutex);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, 0x20, tmp); /* address + write mode */
	ap_cs8427_write_byte(ice, reg | 0x80, tmp); /* incremental mode */
	while (bytes-- > 0)
		ap_cs8427_write_byte(ice, *data++, tmp);
	ap_cs8427_codec_deassert(ice, tmp);
	up(&ice->gpio_mutex);
#else
	while (bytes-- > 0)
		snd_ice1712_ap_cs8427_write(ice, reg++, *data++);
#endif
}

/* sequential read */
static void snd_ice1712_ap_cs8427_read_bytes(ice1712_t *ice, unsigned char reg, int bytes, unsigned char *data)
{
#if 0 // is this working?  -- ti
	unsigned char tmp;
	
	down(&ice->gpio_mutex);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, 0x20, tmp); /* address + write mode */
	ap_cs8427_write_byte(ice, reg | 0x80, tmp); /* incremental mode */
	ap_cs8427_codec_deassert(ice, tmp);
	udelay(5);
	tmp = ap_cs8427_codec_select(ice);
	ap_cs8427_write_byte(ice, 0x21, tmp); /* address + read mode */
	while (bytes-- > 0)
		*data++ = ap_cs8427_read_byte(ice, tmp);
	ap_cs8427_codec_deassert(ice, tmp);
	up(&ice->gpio_mutex);
#else
	while (bytes-- > 0)
		*data++ = snd_ice1712_ap_cs8427_read(ice, reg++); 
#endif
}

#ifdef TARGET_OS2
static ice1712_cs8427_ops_t snd_ice1712_ap_cs8427_ops = {
	snd_ice1712_ap_cs8427_write,
	snd_ice1712_ap_cs8427_read,
	snd_ice1712_ap_cs8427_write_bytes,
	snd_ice1712_ap_cs8427_read_bytes
};
#else
static ice1712_cs8427_ops_t snd_ice1712_ap_cs8427_ops = {
	write: snd_ice1712_ap_cs8427_write,
	read: snd_ice1712_ap_cs8427_read,
	write_bytes: snd_ice1712_ap_cs8427_write_bytes,
	read_bytes: snd_ice1712_ap_cs8427_read_bytes,
};
#endif


/*
 * access via i2c mode (for EWX 24/96, EWS 88MT&D)
 */

/* send SDA and SCL */
static void ewx_i2c_set(ice1712_t *ice, int clk, int data)
{
	unsigned char tmp = 0;
	if (clk)
		tmp |= ICE1712_EWX2496_SERIAL_CLOCK;
	if (data)
		tmp |= ICE1712_EWX2496_SERIAL_DATA;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	udelay(5);
}

/* send one bit */
static void ewx_i2c_send(ice1712_t *ice, int data)
{
	ewx_i2c_set(ice, 0, data);
	ewx_i2c_set(ice, 1, data);
	ewx_i2c_set(ice, 0, data);
}

/* start i2c */
static void ewx_i2c_start(ice1712_t *ice)
{
	ewx_i2c_set(ice, 0, 1);
	ewx_i2c_set(ice, 1, 1);
	ewx_i2c_set(ice, 1, 0);
	ewx_i2c_set(ice, 0, 0);
}

/* stop i2c */
static void ewx_i2c_stop(ice1712_t *ice)
{
	ewx_i2c_set(ice, 0, 0);
	ewx_i2c_set(ice, 1, 0);
	ewx_i2c_set(ice, 1, 1);
}

/* send a byte and get ack */
static void ewx_i2c_write(ice1712_t *ice, unsigned char val)
{
	int i;
	for (i = 7; i >= 0; i--)
		ewx_i2c_send(ice, val & (1 << i));
	ewx_i2c_send(ice, 1); /* ack */
}

/* prepare for write and send i2c_start */
static void ewx_i2c_write_prepare(ice1712_t *ice)
{
	/* set RW high */
	unsigned char mask = ICE1712_EWX2496_RW;
	if (ice->eeprom.subvendor == ICE1712_SUBDEVICE_EWX2496)
		mask |= ICE1712_EWX2496_AK4524_CS; /* CS high also */
	snd_ice1712_gpio_write_bits(ice, mask, mask);
	/* set direction both SDA and SCL write */
	ice->gpio_direction |= ICE1712_EWX2496_SERIAL_CLOCK|ICE1712_EWX2496_SERIAL_DATA;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, ice->gpio_direction);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~(ICE1712_EWX2496_SERIAL_CLOCK|ICE1712_EWX2496_SERIAL_DATA));
			  
	ewx_i2c_start(ice);
}

/* read a bit from serial data;
   this changes write mask
   SDA direction must be set to low
*/
static int ewx_i2c_read_bit(ice1712_t *ice)
{
	int data;
	/* set RW pin to low */
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~ICE1712_EWX2496_RW);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, 0);
	data = (snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & ICE1712_EWX2496_SERIAL_DATA) ? 1 : 0;
	/* set RW pin to high */
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, ICE1712_EWX2496_RW);
	return data;
}

static void ewx_i2c_read_prepare(ice1712_t *ice)
{
	/* set SCL - write, SDA - read */
	ice->gpio_direction |= ICE1712_EWX2496_SERIAL_CLOCK;
	ice->gpio_direction &= ~ICE1712_EWX2496_SERIAL_DATA;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, ice->gpio_direction);
	/* set write mask only to SCL */
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~ICE1712_EWX2496_SERIAL_CLOCK);
}

static void ewx_i2c_read_post(ice1712_t *ice)
{
	/* reset direction both SDA and SCL to write */
	ice->gpio_direction |= ICE1712_EWX2496_SERIAL_CLOCK|ICE1712_EWX2496_SERIAL_DATA;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, ice->gpio_direction);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~(ICE1712_EWX2496_SERIAL_CLOCK|ICE1712_EWX2496_SERIAL_DATA));
}

/* read a byte (and ack bit) */
static unsigned char ewx_i2c_read(ice1712_t *ice)
{
	int i;
	unsigned char data = 0;

	for (i = 7; i >= 0; i--) {
		ewx_i2c_set(ice, 1, 0);
		data |= ewx_i2c_read_bit(ice) << i;
		/* reset write mask */
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK,
				  ~ICE1712_EWX2496_SERIAL_CLOCK);
		ewx_i2c_set(ice, 0, 0);
	}
	/* reset write mask */
	ewx_i2c_read_post(ice);
	ewx_i2c_send(ice, 1); /* ack */
	return data;
}


/*
 * CS8427 on EWX 24/96; Address 0x20
 */
/* write a register */
static void snd_ice1712_ewx_cs8427_write(ice1712_t *ice, unsigned char reg, unsigned char data)
{
	unsigned char saved[2];

	save_gpio_status(ice, saved);
	ewx_i2c_write_prepare(ice);
	ewx_i2c_write(ice, 0x20);  /* address + write */
	ewx_i2c_write(ice, reg); /* MAP */
	ewx_i2c_write(ice, data);
	ewx_i2c_stop(ice);
	restore_gpio_status(ice, saved);
}

/* sequential write */
static void snd_ice1712_ewx_cs8427_write_bytes(ice1712_t *ice, unsigned char reg, int bytes, unsigned char *data)
{
	unsigned char saved[2];

	save_gpio_status(ice, saved);
	ewx_i2c_write_prepare(ice);
	ewx_i2c_write(ice, 0x20);  /* address + write */
	ewx_i2c_write(ice, reg | 0x80); /* MAP incremental mode */
	while (bytes-- > 0)
		ewx_i2c_write(ice, *data++);
	ewx_i2c_stop(ice);
	restore_gpio_status(ice, saved);
}

/* read a register */
static unsigned char snd_ice1712_ewx_cs8427_read(ice1712_t *ice, unsigned char reg)
{
	unsigned char saved[2];
	unsigned char data;

	save_gpio_status(ice, saved);
	ewx_i2c_write_prepare(ice);
	ewx_i2c_write(ice, 0x20);  /* address + write */
	ewx_i2c_write(ice, reg); /* MAP */
	/* we set up the address first but without data */
	ewx_i2c_stop(ice);

	/* now read */
	ewx_i2c_start(ice);
	ewx_i2c_write(ice, 0x21);  /* address + read */
	ewx_i2c_read_prepare(ice);
	data = ewx_i2c_read(ice);
	ewx_i2c_read_post(ice);
	ewx_i2c_stop(ice);

	restore_gpio_status(ice, saved);
	return data;
}

/* sequential read  */
static void snd_ice1712_ewx_cs8427_read_bytes(ice1712_t *ice, unsigned char reg, int bytes, unsigned char *data)
{
#if 0 // the sequential read seems not working (yet)..
	unsigned char saved[2];

	save_gpio_status(ice, saved);
	ewx_i2c_write_prepare(ice);
	ewx_i2c_write(ice, 0x20);  /* address + write */
	ewx_i2c_write(ice, reg | 0x80); /* MAP incremental mode */
	/* we set up the address first but without data */
	ewx_i2c_stop(ice);

	/* now read */
	ewx_i2c_start(ice);
	ewx_i2c_write(ice, 0x21);  /* address + read */
	ewx_i2c_read_prepare(ice);
	while (bytes-- > 0)
		*data++ = ewx_i2c_read(ice);
	ewx_i2c_read_post(ice);
	ewx_i2c_stop(ice);
	restore_gpio_status(ice, saved);
#else
	while (bytes-- > 0)
		*data++ = snd_ice1712_ewx_cs8427_read(ice, reg++); 
#endif
}

#ifdef TARGET_OS2
static ice1712_cs8427_ops_t snd_ice1712_ewx_cs8427_ops = {
	snd_ice1712_ewx_cs8427_write,
	snd_ice1712_ewx_cs8427_read,
	snd_ice1712_ewx_cs8427_write_bytes,
	snd_ice1712_ewx_cs8427_read_bytes,
};
#else
static ice1712_cs8427_ops_t snd_ice1712_ewx_cs8427_ops = {
	write: snd_ice1712_ewx_cs8427_write,
	read: snd_ice1712_ewx_cs8427_read,
	write_bytes: snd_ice1712_ewx_cs8427_write_bytes,
	read_bytes: snd_ice1712_ewx_cs8427_read_bytes,
};
#endif

/*
 * PCF8574 on EWS88MT
 */
static void snd_ice1712_ews88mt_pcf8574_write(ice1712_t *ice, unsigned char addr, unsigned char data)
{
	ewx_i2c_write_prepare(ice);
	ewx_i2c_write(ice, addr); /* address + write */
	ewx_i2c_write(ice, data);
	ewx_i2c_stop(ice);
}

static unsigned char snd_ice1712_ews88mt_pcf8574_read(ice1712_t *ice, unsigned char addr)
{
	unsigned char data;
	ewx_i2c_write_prepare(ice);
	ewx_i2c_write(ice, addr | 0x01);  /* address + read */
	ewx_i2c_read_prepare(ice);
	data = ewx_i2c_read(ice);
	ewx_i2c_read_post(ice);
	ewx_i2c_stop(ice);
	return data;
}

/* AK4524 chip select; address 0x48 bit 0-3 */
static void snd_ice1712_ews88mt_chip_select(ice1712_t *ice, int chip_mask)
{
	unsigned char data, ndata;

	snd_assert(chip_mask >= 0 && chip_mask <= 0x0f, return);
	data = snd_ice1712_ews88mt_pcf8574_read(ice, ICE1712_EWS88MT_OUTPUT_ADDR);
	ndata = (data & 0xf0) | chip_mask;
	if (ndata != data)
		snd_ice1712_ews88mt_pcf8574_write(ice, ICE1712_EWS88MT_OUTPUT_ADDR, ndata);
}


/*
 * PCF8575 on EWS88D (16bit)
 */
static void snd_ice1712_ews88d_pcf8575_write(ice1712_t *ice, unsigned char addr, unsigned short data)
{
	ewx_i2c_write_prepare(ice);
	ewx_i2c_write(ice, addr); /* address + write */
	ewx_i2c_write(ice, data & 0xff);
	ewx_i2c_write(ice, (data >> 8) & 0xff);
	ewx_i2c_stop(ice);
}

static unsigned short snd_ice1712_ews88d_pcf8575_read(ice1712_t *ice, unsigned char addr)
{
	unsigned short data;
	ewx_i2c_write_prepare(ice);
	ewx_i2c_write(ice, addr | 0x01);  /* address + read */
	ewx_i2c_read_prepare(ice);
	data = ewx_i2c_read(ice);
	data |= (unsigned short)ewx_i2c_read(ice) << 8;
	//printk("pcf: read = 0x%x\n", data);
	ewx_i2c_read_post(ice);
	ewx_i2c_stop(ice);
	return data;
}

/*
 * write AK4524 register
 */
static void snd_ice1712_ak4524_write(ice1712_t *ice, int chip,
				     unsigned char addr, unsigned char data)
{
	unsigned char tmp, data_mask, clk_mask, saved[2];
	unsigned char codecs_mask;
	int idx, cif;
	unsigned int addrdata;

	snd_assert(chip >=0 && chip < 4, return);
	if (ice->eeprom.subvendor == ICE1712_SUBDEVICE_EWS88MT) {
		/* assert AK4524 CS */
		snd_ice1712_ews88mt_chip_select(ice, ~(1 << chip) & 0x0f);
		//snd_ice1712_ews88mt_chip_select(ice, 0x0f);
	}

	cif = 0; /* the default level of the CIF pin from AK4524 */
	save_gpio_status(ice, saved);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		data_mask = ICE1712_DELTA_AP_DOUT;
		clk_mask = ICE1712_DELTA_AP_CCLK;
		codecs_mask = ICE1712_DELTA_AP_CS_CODEC; /* select AK4528 codec */
		tmp |= ICE1712_DELTA_AP_CS_DIGITAL; /* assert digital high */
		break;
	case ICE1712_SUBDEVICE_EWX2496:
		data_mask = ICE1712_EWX2496_SERIAL_DATA;
		clk_mask = ICE1712_EWX2496_SERIAL_CLOCK;
		codecs_mask = ICE1712_EWX2496_AK4524_CS;
		tmp |= ICE1712_EWX2496_RW; /* set rw bit high */
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION,
				  ice->gpio_direction | data_mask | clk_mask |
				  codecs_mask | ICE1712_EWX2496_RW);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK,
				  ~(data_mask | clk_mask |
				    codecs_mask | ICE1712_EWX2496_RW));
		cif = 1; /* CIF high */
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
		data_mask = ICE1712_EWS88_SERIAL_DATA;
		clk_mask = ICE1712_EWS88_SERIAL_CLOCK;
		codecs_mask = 0; /* no chip select on gpio */
		tmp |= ICE1712_EWS88_RW; /* set rw bit high */
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION,
				  ice->gpio_direction | data_mask | clk_mask |
				  codecs_mask | ICE1712_EWS88_RW);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK,
				  ~(data_mask | clk_mask |
				    codecs_mask | ICE1712_EWS88_RW));
		cif = 1; /* CIF high */
		break;
	default:
		data_mask = ICE1712_DELTA_CODEC_SERIAL_DATA;
		clk_mask = ICE1712_DELTA_CODEC_SERIAL_CLOCK;
		codecs_mask = chip == 0 ? ICE1712_DELTA_CODEC_CHIP_A : ICE1712_DELTA_CODEC_CHIP_B;
		break;
	}

	if (cif) {
		tmp |= codecs_mask; /* start without chip select */
	} else {
		tmp &= ~codecs_mask; /* chip select low */
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(1);
	}

	addr &= 0x07;
	/* build I2C address + data byte */
	addrdata = 0xa000 | (addr << 8) | data;
	for (idx = 15; idx >= 0; idx--) {
		tmp &= ~(data_mask|clk_mask);
		if (addrdata & (1 << idx))
			tmp |= data_mask;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		//udelay(200);
		udelay(1);
		tmp |= clk_mask;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(1);
	}

	if (ice->eeprom.subvendor == ICE1712_SUBDEVICE_EWS88MT) {
		//snd_ice1712_ews88mt_chip_select(ice, ~(1 << chip) & 0x0f);
		udelay(1);
		snd_ice1712_ews88mt_chip_select(ice, 0x0f);
	} else {
		if (cif) {
			/* assert a cs pulse to trigger */
			tmp &= ~codecs_mask;
			snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
			udelay(1);
		}
		tmp |= codecs_mask; /* chip select high to trigger */
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		udelay(1);
	}

	ice->ak4524_images[chip][addr] = data;
	restore_gpio_status(ice, saved);
}


/*
 * decode/encode channel status bits from CS8404A on EWS88MT&D
 */
static void snd_ice1712_ews_decode_cs8404_spdif_bits(snd_aes_iec958_t *diga,
						     unsigned char bits)
{
	if (bits & 0x10) {	/* consumer */
		if (!(bits & 0x20))
			diga->status[0] |= IEC958_AES0_CON_NOT_COPYRIGHT;
		if (!(bits & 0x40))
			diga->status[0] |= IEC958_AES0_CON_EMPHASIS_5015;
		if (!(bits & 0x80))
			diga->status[1] |= IEC958_AES1_CON_ORIGINAL;
		switch (bits & 0x03) {
		case 0x00: diga->status[1] |= IEC958_AES1_CON_DAT; break;
		case 0x03: diga->status[1] |= IEC958_AES1_CON_GENERAL; break;
		}
		switch (bits & 0x06) {
		case 0x02: diga->status[3] |= IEC958_AES3_CON_FS_32000; break;
		case 0x04: diga->status[3] |= IEC958_AES3_CON_FS_48000; break;
		case 0x06: diga->status[3] |= IEC958_AES3_CON_FS_44100; break;
		}
	} else {
		diga->status[0] = IEC958_AES0_PROFESSIONAL;
		if (!(bits & 0x04))
			diga->status[0] |= IEC958_AES0_NONAUDIO;
		switch (bits & 0x60) {
		case 0x00: diga->status[0] |= IEC958_AES0_PRO_FS_32000; break;
		case 0x40: diga->status[0] |= IEC958_AES0_PRO_FS_44100; break;
		case 0x20: diga->status[0] |= IEC958_AES0_PRO_FS_48000; break;
		case 0x60: diga->status[0] |= IEC958_AES0_PRO_FS_NOTID; break;
		}
		switch (bits & 0x03) {
		case 0x02: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_NONE; break;
		case 0x01: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_5015; break;
		case 0x00: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_CCITT; break;
		case 0x03: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_NOTID; break;
		}
		if (!(bits & 0x80))
			diga->status[1] |= IEC958_AES1_PRO_MODE_STEREOPHONIC;
	}
}

static unsigned char snd_ice1712_ews_encode_cs8404_spdif_bits(snd_aes_iec958_t *diga)
{
	unsigned char bits;

	if (!(diga->status[0] & IEC958_AES0_PROFESSIONAL)) {
		bits = 0x10;	/* consumer mode */
		if (!(diga->status[0] & IEC958_AES0_CON_NOT_COPYRIGHT))
			bits |= 0x20;
		if ((diga->status[0] & IEC958_AES0_CON_EMPHASIS) == IEC958_AES0_CON_EMPHASIS_NONE)
			bits |= 0x40;
		if (!(diga->status[1] & IEC958_AES1_CON_ORIGINAL))
			bits |= 0x80;
		if ((diga->status[1] & IEC958_AES1_CON_CATEGORY) == IEC958_AES1_CON_GENERAL)
			bits |= 0x03;
		switch (diga->status[3] & IEC958_AES3_CON_FS) {
		default:
		case IEC958_AES3_CON_FS_44100: bits |= 0x06; break;
		case IEC958_AES3_CON_FS_48000: bits |= 0x04; break;
		case IEC958_AES3_CON_FS_32000: bits |= 0x02; break;
		}
	} else {
		bits = 0x00;	/* professional mode */
		if (!(diga->status[0] & IEC958_AES0_NONAUDIO))
			bits |= 0x04;
		switch (diga->status[0] & IEC958_AES0_PRO_FS) {
		case IEC958_AES0_PRO_FS_32000:	bits |= 0x00; break;
		case IEC958_AES0_PRO_FS_44100:	bits |= 0x40; break;	/* 44.1kHz */
		case IEC958_AES0_PRO_FS_48000:	bits |= 0x20; break;	/* 48kHz */
		default:
		case IEC958_AES0_PRO_FS_NOTID:	bits |= 0x00; break;
		}
		switch (diga->status[0] & IEC958_AES0_PRO_EMPHASIS) {
		case IEC958_AES0_PRO_EMPHASIS_NONE: bits |= 0x02; break;
		case IEC958_AES0_PRO_EMPHASIS_5015: bits |= 0x01; break;
		case IEC958_AES0_PRO_EMPHASIS_CCITT: bits |= 0x00; break;
		default:
		case IEC958_AES0_PRO_EMPHASIS_NOTID: bits |= 0x03; break;
		}
		switch (diga->status[1] & IEC958_AES1_PRO_MODE) {
		case IEC958_AES1_PRO_MODE_TWO:
		case IEC958_AES1_PRO_MODE_STEREOPHONIC: bits |= 0x00; break;
		default: bits |= 0x80; break;
		}
	}
	return bits;
}

/* write on i2c address 0x40 */
static void snd_ice1712_ews_cs8404_spdif_write(ice1712_t *ice, unsigned char bits)
{
	unsigned short val, nval;

	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWS88MT:
		snd_ice1712_ews88mt_pcf8574_write(ice, ICE1712_EWS88MT_CS8404_ADDR, bits);
		break;
	case ICE1712_SUBDEVICE_EWS88D:
		val = snd_ice1712_ews88d_pcf8575_read(ice, ICE1712_EWS88D_PCF_ADDR);
		nval = val & 0xff;
		nval |= bits << 8;
		if (val != nval)
			snd_ice1712_ews88d_pcf8575_write(ice, ICE1712_EWS88D_PCF_ADDR, nval);
		break;
	}
}


/*
 * change the input clock selection
 * spdif_clock = 1 - IEC958 input, 0 - Envy24
 */
static int snd_ice1712_cs8427_set_input_clock(ice1712_t *ice, int spdif_clock)
{
	unsigned char val, nval;
	val = ice->cs8427_ops->read(ice, 4);
	nval = val & 0xf0;
	if (spdif_clock)
		nval |= 0x01;
	else
		nval |= 0x04;
	if (val != nval) {
		ice->cs8427_ops->write(ice, 4, nval);
		return 1;
	}
	return 0;
}

#if 0 // we change clock selection automatically according to the ice1712 clock source
static int snd_ice1712_cs8427_clock_select_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_cs8427_clock_select_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int val;
	val = ice->cs8427_ops->read(ice, 4);
	ucontrol->value.integer.value[0] = (val & 0x01) ? 1 : 0;
	return 0;
}

static int snd_ice1712_cs8427_clock_select_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	return snd_ice1712_cs8427_set_input_clock(ice, ucontrol->value.integer.value[0]);
}

static snd_kcontrol_new_t snd_ice1712_cs8427_clock_select = {
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "CS8427 Clock Select",
	info: snd_ice1712_cs8427_clock_select_info,
	get: snd_ice1712_cs8427_clock_select_get,
	put: snd_ice1712_cs8427_clock_select_put,
};
#endif


/*
 */
static int snd_ice1712_cs8427_in_status_info(snd_kcontrol_t *kcontrol,
					     snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_ice1712_cs8427_in_status_get(snd_kcontrol_t *kcontrol,
					    snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = ice->cs8427_ops->read(ice, 15);
	return 0;
}

static snd_kcontrol_new_t snd_ice1712_cs8427_in_status = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	"IEC958 Input Status",0,
	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	snd_ice1712_cs8427_in_status_info,
	snd_ice1712_cs8427_in_status_get,0,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_PCM,
	info: snd_ice1712_cs8427_in_status_info,
	name: "IEC958 Input Status",
	access: SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	get: snd_ice1712_cs8427_in_status_get,
#endif
};


/*
 */
static void snd_ice1712_set_pro_rate(ice1712_t *ice, snd_pcm_substream_t *substream)
{
	unsigned long flags;
	unsigned int rate;
	unsigned char val, tmp;

	spin_lock_irqsave(&ice->reg_lock, flags);
	if (inb(ICEMT(ice, PLAYBACK_CONTROL)) & (ICE1712_CAPTURE_START_SHADOW|
						 ICE1712_PLAYBACK_PAUSE|
						 ICE1712_PLAYBACK_START))
		goto __end;
	if (inb(ICEMT(ice, RATE)) & ICE1712_SPDIF_MASTER)
		goto __end;
	rate = substream->runtime->rate;
	switch (rate) {
	case 8000: val = 6; break;
	case 9600: val = 3; break;
	case 11025: val = 10; break;
	case 12000: val = 2; break;
	case 16000: val = 5; break;
	case 22050: val = 9; break;
	case 24000: val = 1; break;
	case 32000: val = 4; break;
	case 44100: val = 8; break;
	case 48000: val = 0; break;
	case 64000: val = 15; break;
	case 88200: val = 11; break;
	case 96000: val = 7; break;
	default:
		snd_BUG();
		val = 0;
		break;
	}
	outb(val, ICEMT(ice, RATE));
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_DELTA44:
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		/* FIXME: we should put AK452x codecs to reset state
		   when this bit is being changed */
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		down(&ice->gpio_mutex);
		tmp = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
		if (val == 15 || val == 11 || val == 7) {
			tmp |= ICE1712_DELTA_DFS;
		} else {
			tmp &= ~ICE1712_DELTA_DFS;
		}
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
		up(&ice->gpio_mutex);
		return;
	}
      __end:
	spin_unlock_irqrestore(&ice->reg_lock, flags);
}

/*
 *  Interrupt handler
 */

static void snd_ice1712_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, dev_id, return);
	unsigned char status;

	while (1) {
		status = inb(ICEREG(ice, IRQSTAT));
		if (status == 0)
			break;
		if (status & ICE1712_IRQ_MPU1) {
			if (ice->rmidi[0])
				snd_mpu401_uart_interrupt(irq, ice->rmidi[0]->private_data, regs);
			outb(ICE1712_IRQ_MPU1, ICEREG(ice, IRQSTAT));
			status &= ~ICE1712_IRQ_MPU1;
		}
		if (status & ICE1712_IRQ_TIMER)
			outb(ICE1712_IRQ_TIMER, ICEREG(ice, IRQSTAT));
		if (status & ICE1712_IRQ_MPU2) {
			if (ice->rmidi[1])
				snd_mpu401_uart_interrupt(irq, ice->rmidi[1]->private_data, regs);
			outb(ICE1712_IRQ_MPU2, ICEREG(ice, IRQSTAT));
			status &= ~ICE1712_IRQ_MPU2;
		}
		if (status & ICE1712_IRQ_PROPCM) {
			unsigned char mtstat = inb(ICEMT(ice, IRQ));
			if (mtstat & ICE1712_MULTI_PBKSTATUS) {
				if (ice->playback_pro_substream)
					snd_pcm_period_elapsed(ice->playback_pro_substream);
				outb(ICE1712_MULTI_PBKSTATUS, ICEMT(ice, IRQ));
			}
			if (mtstat & ICE1712_MULTI_CAPSTATUS) {
				if (ice->capture_pro_substream)
					snd_pcm_period_elapsed(ice->capture_pro_substream);
				outb(ICE1712_MULTI_CAPSTATUS, ICEMT(ice, IRQ));
			}
		}
		if (status & ICE1712_IRQ_FM)
			outb(ICE1712_IRQ_FM, ICEREG(ice, IRQSTAT));
		if (status & ICE1712_IRQ_PBKDS) {
			u32 idx;
			u16 pbkstatus;
			snd_pcm_substream_t *substream;
			pbkstatus = inw(ICEDS(ice, INTSTAT));
			//printk("pbkstatus = 0x%x\n", pbkstatus);
			for (idx = 0; idx < 6; idx++) {
				if ((pbkstatus & (3 << (idx * 2))) == 0)
					continue;
				if ((substream = ice->playback_con_substream_ds[idx]) != NULL)
					snd_pcm_period_elapsed(substream);
				outw(3 << (idx * 2), ICEDS(ice, INTSTAT));
			}
			outb(ICE1712_IRQ_PBKDS, ICEREG(ice, IRQSTAT));
		}
		if (status & ICE1712_IRQ_CONCAP) {
			if (ice->capture_con_substream)
				snd_pcm_period_elapsed(ice->capture_con_substream);
			outb(ICE1712_IRQ_CONCAP, ICEREG(ice, IRQSTAT));
		}
		if (status & ICE1712_IRQ_CONPBK) {
			if (ice->playback_con_substream)
				snd_pcm_period_elapsed(ice->playback_con_substream);
			outb(ICE1712_IRQ_CONPBK, ICEREG(ice, IRQSTAT));
		}
	}
}

/*
 *  PCM part - misc
 */

static int snd_ice1712_hw_params(snd_pcm_substream_t * substream,
				 snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_ice1712_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/*
 *  PCM part - consumer I/O
 */

static int snd_ice1712_playback_trigger(snd_pcm_substream_t * substream,
					int cmd)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	int result = 0;
	u32 tmp;
	
	spin_lock(&ice->reg_lock);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_PBK_CTRL);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		tmp |= 1;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		tmp &= ~1;
	} else if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH) {
		tmp |= 2;
	} else if (cmd == SNDRV_PCM_TRIGGER_PAUSE_RELEASE) {
		tmp &= ~2;
	} else {
		result = -EINVAL;
	}
	snd_ice1712_write(ice, ICE1712_IREG_PBK_CTRL, tmp);
	spin_unlock(&ice->reg_lock);
	return result;
}

static int snd_ice1712_playback_ds_trigger(snd_pcm_substream_t * substream,
					   int cmd)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	int result = 0;
	u32 tmp;
	
	spin_lock(&ice->reg_lock);
	tmp = snd_ice1712_ds_read(ice, substream->number * 2, ICE1712_DSC_CONTROL);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		tmp |= 1;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		tmp &= ~1;
	} else if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH) {
		tmp |= 2;
	} else if (cmd == SNDRV_PCM_TRIGGER_PAUSE_RELEASE) {
		tmp &= ~2;
	} else {
		result = -EINVAL;
	}
	snd_ice1712_ds_write(ice, substream->number * 2, ICE1712_DSC_CONTROL, tmp);
	spin_unlock(&ice->reg_lock);
	return result;
}

static int snd_ice1712_capture_trigger(snd_pcm_substream_t * substream,
				       int cmd)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	int result = 0;
	u8 tmp;
	
	spin_lock(&ice->reg_lock);
	tmp = snd_ice1712_read(ice, ICE1712_IREG_CAP_CTRL);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		tmp |= 1;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		tmp &= ~1;
	} else {
		result = -EINVAL;
	}
	snd_ice1712_write(ice, ICE1712_IREG_CAP_CTRL, tmp);
	spin_unlock(&ice->reg_lock);
	return result;
}

static int snd_ice1712_playback_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	u32 period_size, buf_size, rate, tmp;

	period_size = (snd_pcm_lib_period_bytes(substream) >> 2) - 1;
	buf_size = snd_pcm_lib_buffer_bytes(substream) - 1;
	tmp = 0x0000;
	if (snd_pcm_format_width(runtime->format) == 16)
		tmp |= 0x10;
	if (runtime->channels == 2)
		tmp |= 0x08;
	rate = (runtime->rate * 8192) / 375;
	if (rate > 0x000fffff)
		rate = 0x000fffff;
	spin_lock_irqsave(&ice->reg_lock, flags);
	outb(0, ice->ddma_port + 15);
	outb(ICE1712_DMA_MODE_WRITE | ICE1712_DMA_AUTOINIT, ice->ddma_port + 0x0b);
	outl(runtime->dma_addr, ice->ddma_port + 0);
	outw(buf_size, ice->ddma_port + 4);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_RATE_LO, rate & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_RATE_MID, (rate >> 8) & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_RATE_HI, (rate >> 16) & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_CTRL, tmp);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_COUNT_LO, period_size & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_COUNT_HI, period_size >> 8);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_LEFT, 0);
	snd_ice1712_write(ice, ICE1712_IREG_PBK_RIGHT, 0);
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static int snd_ice1712_playback_ds_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	u32 period_size, buf_size, rate, tmp, chn;

	period_size = snd_pcm_lib_period_bytes(substream) - 1;
	buf_size = snd_pcm_lib_buffer_bytes(substream) - 1;
	tmp = 0x0064;
	if (snd_pcm_format_width(runtime->format) == 16)
		tmp &= ~0x04;
	if (runtime->channels == 2)
		tmp |= 0x08;
	rate = (runtime->rate * 8192) / 375;
	if (rate > 0x000fffff)
		rate = 0x000fffff;
	ice->playback_con_active_buf[substream->number] = 0;
	ice->playback_con_virt_addr[substream->number] = runtime->dma_addr;
	chn = substream->number * 2;
	spin_lock_irqsave(&ice->reg_lock, flags);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_ADDR0, runtime->dma_addr);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_COUNT0, period_size);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_ADDR1, runtime->dma_addr + (runtime->periods > 1 ? period_size + 1 : 0));
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_COUNT1, period_size);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_RATE, rate);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_VOLUME, 0);
	snd_ice1712_ds_write(ice, chn, ICE1712_DSC_CONTROL, tmp);
	if (runtime->channels == 2) {
		snd_ice1712_ds_write(ice, chn + 1, ICE1712_DSC_RATE, rate);
		snd_ice1712_ds_write(ice, chn + 1, ICE1712_DSC_VOLUME, 0);
	}
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static int snd_ice1712_capture_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	u32 period_size, buf_size;
	u8 tmp;

	period_size = (snd_pcm_lib_period_bytes(substream) >> 2) - 1;
	buf_size = snd_pcm_lib_buffer_bytes(substream) - 1;
	tmp = 0x06;
	if (snd_pcm_format_width(runtime->format) == 16)
		tmp &= ~0x04;
	if (runtime->channels == 2)
		tmp &= ~0x02;
	spin_lock_irqsave(&ice->reg_lock, flags);
	outl(ice->capture_con_virt_addr = runtime->dma_addr, ICEREG(ice, CONCAP_ADDR));
	outw(buf_size, ICEREG(ice, CONCAP_COUNT));
	snd_ice1712_write(ice, ICE1712_IREG_CAP_COUNT_HI, period_size >> 8);
	snd_ice1712_write(ice, ICE1712_IREG_CAP_COUNT_LO, period_size & 0xff);
	snd_ice1712_write(ice, ICE1712_IREG_CAP_CTRL, tmp);
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	snd_ac97_set_rate(ice->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	return 0;
}

static snd_pcm_uframes_t snd_ice1712_playback_pointer(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	size_t ptr;

	if (!(snd_ice1712_read(ice, ICE1712_IREG_PBK_CTRL) & 1))
		return 0;
	ptr = runtime->buffer_size - inw(ice->ddma_port + 4);
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_ice1712_playback_ds_pointer(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	u8 addr;
	size_t ptr;

	if (!(snd_ice1712_ds_read(ice, substream->number * 2, ICE1712_DSC_CONTROL) & 1))
		return 0;
	if (ice->playback_con_active_buf[substream->number])
		addr = ICE1712_DSC_ADDR1;
	else
		addr = ICE1712_DSC_ADDR0;
	ptr = snd_ice1712_ds_read(ice, substream->number * 2, addr) -
		ice->playback_con_virt_addr[substream->number];
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_ice1712_capture_pointer(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(snd_ice1712_read(ice, ICE1712_IREG_CAP_CTRL) & 1))
		return 0;
	ptr = inl(ICEREG(ice, CONCAP_ADDR)) - ice->capture_con_virt_addr;
	return bytes_to_frames(substream->runtime, ptr);
}

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_ice1712_playback =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	(64*1024),
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	(64*1024),
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

static snd_pcm_hardware_t snd_ice1712_playback_ds =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	(128*1024),
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	(128*1024),
/*	periods_min:	  */	2,
/*	periods_max:	  */	2,
/*	fifo_size:	  */	0,
};

static snd_pcm_hardware_t snd_ice1712_capture =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max:  */	(64*1024),
/*	period_bytes_min:  */	64,
/*	period_bytes_max:  */	(64*1024),
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};
#else
static snd_pcm_hardware_t snd_ice1712_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(64*1024),
	period_bytes_min:	64,
	period_bytes_max:	(64*1024),
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_ice1712_playback_ds =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(128*1024),
	period_bytes_min:	64,
	period_bytes_max:	(128*1024),
	periods_min:		2,
	periods_max:		2,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_ice1712_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(64*1024),
	period_bytes_min:	64,
	period_bytes_max:	(64*1024),
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};
#endif

static int snd_ice1712_playback_open(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	ice1712_t *ice = snd_pcm_substream_chip(substream);

	ice->playback_con_substream = substream;
	runtime->hw = snd_ice1712_playback;
	return 0;
}

static int snd_ice1712_playback_ds_open(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	unsigned long flags;
	u32 tmp;

	ice->playback_con_substream_ds[substream->number] = substream;
	runtime->hw = snd_ice1712_playback_ds;
	spin_lock_irqsave(&ice->reg_lock, flags); 
	tmp = inw(ICEDS(ice, INTMASK)) & ~(1 << (substream->number * 2));
	outw(tmp, ICEDS(ice, INTMASK));
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static int snd_ice1712_capture_open(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	ice1712_t *ice = snd_pcm_substream_chip(substream);

	ice->capture_con_substream = substream;
	runtime->hw = snd_ice1712_capture;
	runtime->hw.rates = ice->ac97->rates_adc;
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	return 0;
}

static int snd_ice1712_playback_close(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);

	ice->playback_con_substream = NULL;
	return 0;
}

static int snd_ice1712_playback_ds_close(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&ice->reg_lock, flags); 
	tmp = inw(ICEDS(ice, INTMASK)) | (3 << (substream->number * 2));
	outw(tmp, ICEDS(ice, INTMASK));
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	ice->playback_con_substream_ds[substream->number] = NULL;
	return 0;
}

static int snd_ice1712_capture_close(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);

	/* disable ADC power */
	snd_ac97_update_bits(ice->ac97, AC97_POWERDOWN, 0x0100, 0x0100);
	ice->capture_con_substream = NULL;
	return 0;
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_ice1712_playback_ops = {
	snd_ice1712_playback_open,
	snd_ice1712_playback_close,
	snd_pcm_lib_ioctl,
	snd_ice1712_hw_params,
	snd_ice1712_hw_free,
	snd_ice1712_playback_prepare,
	snd_ice1712_playback_trigger,
	snd_ice1712_playback_pointer,0,0
};

static snd_pcm_ops_t snd_ice1712_playback_ds_ops = {
	snd_ice1712_playback_ds_open,
	snd_ice1712_playback_ds_close,
	snd_pcm_lib_ioctl,
	snd_ice1712_hw_params,
	snd_ice1712_hw_free,
	snd_ice1712_playback_ds_prepare,
	snd_ice1712_playback_ds_trigger,
	snd_ice1712_playback_ds_pointer,0,0
};

static snd_pcm_ops_t snd_ice1712_capture_ops = {
	snd_ice1712_capture_open,
	snd_ice1712_capture_close,
	snd_pcm_lib_ioctl,
	snd_ice1712_hw_params,
	snd_ice1712_hw_free,
	snd_ice1712_capture_prepare,
	snd_ice1712_capture_trigger,
	snd_ice1712_capture_pointer,0,0
};
#else
static snd_pcm_ops_t snd_ice1712_playback_ops = {
	open:		snd_ice1712_playback_open,
	close:		snd_ice1712_playback_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_ice1712_hw_params,
	hw_free:	snd_ice1712_hw_free,
	prepare:	snd_ice1712_playback_prepare,
	trigger:	snd_ice1712_playback_trigger,
	pointer:	snd_ice1712_playback_pointer,
};

static snd_pcm_ops_t snd_ice1712_playback_ds_ops = {
	open:		snd_ice1712_playback_ds_open,
	close:		snd_ice1712_playback_ds_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_ice1712_hw_params,
	hw_free:	snd_ice1712_hw_free,
	prepare:	snd_ice1712_playback_ds_prepare,
	trigger:	snd_ice1712_playback_ds_trigger,
	pointer:	snd_ice1712_playback_ds_pointer,
};

static snd_pcm_ops_t snd_ice1712_capture_ops = {
	open:		snd_ice1712_capture_open,
	close:		snd_ice1712_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_ice1712_hw_params,
	hw_free:	snd_ice1712_hw_free,
	prepare:	snd_ice1712_capture_prepare,
	trigger:	snd_ice1712_capture_trigger,
	pointer:	snd_ice1712_capture_pointer,
};
#endif

static void __exit snd_ice1712_pcm_free(snd_pcm_t *pcm)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, pcm->private_data, return);
	ice->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __init snd_ice1712_pcm(ice1712_t * ice, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(ice->card, "ICE1712 consumer", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ice1712_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ice1712_capture_ops);

	pcm->private_data = ice;
	pcm->private_free = snd_ice1712_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, "ICE1712 consumer");
	ice->pcm = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(ice->pci, pcm, 64*1024, 64*1024);

	if (rpcm)
		*rpcm = pcm;

	printk("Consumer PCM code does not work well at the moment --jk\n");

	return 0;
}

static void __exit snd_ice1712_pcm_free_ds(snd_pcm_t *pcm)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, pcm->private_data, return);
	ice->pcm_ds = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __init snd_ice1712_pcm_ds(ice1712_t * ice, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(ice->card, "ICE1712 consumer (DS)", device, 6, 0, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ice1712_playback_ds_ops);

	pcm->private_data = ice;
	pcm->private_free = snd_ice1712_pcm_free_ds;
	pcm->info_flags = 0;
	strcpy(pcm->name, "ICE1712 consumer (DS)");
	ice->pcm_ds = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(ice->pci, pcm, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = pcm;

	return 0;
}

/*
 *  PCM code - professional part (multitrack)
 */

static unsigned int rates[] = { 8000, 9600, 11025, 12000, 16000, 22050, 24000,
				32000, 44100, 48000, 64000, 88200, 96000 };

#define RATES sizeof(rates) / sizeof(rates[0])

#ifdef TARGET_OS2
static snd_pcm_hw_constraint_list_t hw_constraints_rates = {
	RATES,
	rates,
	0,
};
#else
static snd_pcm_hw_constraint_list_t hw_constraints_rates = {
	count: RATES,
	list: rates,
	mask: 0,
};
#endif

#if 0

static int snd_ice1712_playback_pro_ioctl(snd_pcm_substream_t * substream,
					  unsigned int cmd,
					  void *arg)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_IOCTL1_DIG_INFO:
	{
		snd_pcm_dig_info_t *info = arg;
		switch (ice->eeprom.subvendor) {
		case ICE1712_SUBDEVICE_DELTA1010:
		case ICE1712_SUBDEVICE_DELTADIO2496:
		case ICE1712_SUBDEVICE_DELTA66:
			return snd_ice1712_dig_cs8403_info(substream, info);
		}
	}
	case SNDRV_PCM_IOCTL1_DIG_PARAMS:
	{
		snd_pcm_dig_params_t *params = arg;
		switch (ice->eeprom.subvendor) {
		case ICE1712_SUBDEVICE_DELTA1010:
		case ICE1712_SUBDEVICE_DELTADIO2496:
		case ICE1712_SUBDEVICE_DELTA66:
			return snd_ice1712_dig_cs8403_params(substream, params);
		}
	}
	default:
		break;
	}
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

#endif

static int snd_ice1712_pro_trigger(snd_pcm_substream_t *substream,
				   int cmd)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	{
		unsigned int what;
		unsigned int old;
		if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
			return -EINVAL;
		what = ICE1712_PLAYBACK_PAUSE;
		snd_pcm_trigger_done(substream, substream);
		spin_lock(&ice->reg_lock);
		old = inl(ICEMT(ice, PLAYBACK_CONTROL));
		if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH)
			old |= what;
		else
			old &= ~what;
		outl(old, ICEMT(ice, PLAYBACK_CONTROL));
		spin_unlock(&ice->reg_lock);
		break;
	}
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
	{
		unsigned int what = 0;
		unsigned int old;
		snd_pcm_substream_t *s = substream;
		do {
			if (s == ice->playback_pro_substream) {
				what |= ICE1712_PLAYBACK_START;
				snd_pcm_trigger_done(s, substream);
			} else if (s == ice->capture_pro_substream) {
				what |= ICE1712_CAPTURE_START_SHADOW;
				snd_pcm_trigger_done(s, substream);
			}
			s = s->link_next;
		} while (s != substream);
		spin_lock(&ice->reg_lock);
		old = inl(ICEMT(ice, PLAYBACK_CONTROL));
		if (cmd == SNDRV_PCM_TRIGGER_START)
			old |= what;
		else
			old &= ~what;
		outl(old, ICEMT(ice, PLAYBACK_CONTROL));
		spin_unlock(&ice->reg_lock);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static void snd_ice1712_cs8427_set_status(ice1712_t *ice, snd_pcm_runtime_t *runtime,
					  unsigned char *status)
{
	if (status[0] & IEC958_AES0_PROFESSIONAL) {
		status[0] &= ~IEC958_AES0_PRO_FS;
		switch (runtime->rate) {
		case 32000: status[0] |= IEC958_AES0_PRO_FS_32000; break;
		case 44100: status[0] |= IEC958_AES0_PRO_FS_44100; break;
		case 48000: status[0] |= IEC958_AES0_PRO_FS_48000; break;
		default: status[0] |= IEC958_AES0_PRO_FS_NOTID; break;
		}
	} else {
		status[3] &= ~IEC958_AES3_CON_FS;
		switch (runtime->rate) {
		case 32000: status[3] |= IEC958_AES3_CON_FS_32000; break;
		case 44100: status[3] |= IEC958_AES3_CON_FS_44100; break;
		case 48000: status[3] |= IEC958_AES3_CON_FS_48000; break;
		}
	}
}

static int snd_ice1712_playback_pro_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	unsigned int tmp;
	unsigned char tmpst[5];
	int change;

	ice->playback_pro_size = snd_pcm_lib_buffer_bytes(substream);
	snd_ice1712_set_pro_rate(ice, substream);
	spin_lock_irqsave(&ice->reg_lock, flags);
	outl(substream->runtime->dma_addr, ICEMT(ice, PLAYBACK_ADDR));
	outw((ice->playback_pro_size >> 2) - 1, ICEMT(ice, PLAYBACK_SIZE));
	outw((snd_pcm_lib_period_bytes(substream) >> 2) - 1, ICEMT(ice, PLAYBACK_COUNT));
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
		/* setup S/PDIF */
		tmp = ice->cs8403_spdif_stream_bits;
		if (tmp & 0x01)		/* consumer */
			tmp &= (tmp & 0x01) ? ~0x06 : ~0x18;
		switch (substream->runtime->rate) {
		case 32000: tmp |= (tmp & 0x01) ? 0x04 : 0x00; break;
		case 44100: tmp |= (tmp & 0x01) ? 0x00 : 0x10; break;
		case 48000: tmp |= (tmp & 0x01) ? 0x02 : 0x08; break;
		default: tmp |= (tmp & 0x01) ? 0x00 : 0x18; break;
		}
		change = ice->cs8403_spdif_stream_bits != tmp;
		ice->cs8403_spdif_stream_bits = tmp;
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		if (change)
			snd_ctl_notify(ice->card, SNDRV_CTL_EVENT_MASK_VALUE, &ice->spdif_stream_ctl->id);
		snd_ice1712_delta_cs8403_spdif_write(ice, tmp);
		return 0;
	case ICE1712_SUBDEVICE_EWX2496:
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		/* setup S/PDIF */
		memcpy(tmpst, ice->cs8427_spdif_stream_status, 5);
		snd_ice1712_cs8427_set_status(ice, substream->runtime, tmpst); 
		change = memcmp(tmpst, ice->cs8427_spdif_stream_status, 5) != 0;
		memcpy(ice->cs8427_spdif_stream_status, tmpst, 5);
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		if (change) {
			snd_ctl_notify(ice->card, SNDRV_CTL_EVENT_MASK_VALUE, &ice->spdif_stream_ctl->id);
			ice->cs8427_ops->write_bytes(ice, 32, 5, tmpst);
		}
		return 0;
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		/* setup S/PDIF */
		tmp = ice->cs8403_spdif_stream_bits;
		if (tmp & 0x10)		/* consumer */
			tmp &= (tmp & 0x01) ? ~0x06 : ~0x60;
		switch (substream->runtime->rate) {
		case 32000: tmp |= (tmp & 0x01) ? 0x02 : 0x00; break;
		case 44100: tmp |= (tmp & 0x01) ? 0x06 : 0x40; break;
		case 48000: tmp |= (tmp & 0x01) ? 0x04 : 0x20; break;
		default: tmp |= (tmp & 0x01) ? 0x06 : 0x40; break;
		}
		change = ice->cs8403_spdif_stream_bits != tmp;
		ice->cs8403_spdif_stream_bits = tmp;
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		if (change)
			snd_ctl_notify(ice->card, SNDRV_CTL_EVENT_MASK_VALUE, &ice->spdif_stream_ctl->id);
		snd_ice1712_ews_cs8404_spdif_write(ice, tmp);
		return 0;
	}

	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static int snd_ice1712_capture_pro_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ice1712_t *ice = snd_pcm_substream_chip(substream);

	ice->capture_pro_size = snd_pcm_lib_buffer_bytes(substream);
	snd_ice1712_set_pro_rate(ice, substream);
	spin_lock_irqsave(&ice->reg_lock, flags);
	outl(substream->runtime->dma_addr, ICEMT(ice, CAPTURE_ADDR));
	outw((ice->capture_pro_size >> 2) - 1, ICEMT(ice, CAPTURE_SIZE));
	outw((snd_pcm_lib_period_bytes(substream) >> 2) - 1, ICEMT(ice, CAPTURE_COUNT));
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static snd_pcm_uframes_t snd_ice1712_playback_pro_pointer(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(inl(ICEMT(ice, PLAYBACK_CONTROL)) & ICE1712_PLAYBACK_START))
		return 0;
	ptr = ice->playback_pro_size - (inw(ICEMT(ice, PLAYBACK_SIZE)) << 2);
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_ice1712_capture_pro_pointer(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(inl(ICEMT(ice, PLAYBACK_CONTROL)) & ICE1712_CAPTURE_START_SHADOW))
		return 0;
	ptr = ice->capture_pro_size - (inw(ICEMT(ice, CAPTURE_SIZE)) << 2);
	return bytes_to_frames(substream->runtime, ptr);
}

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_ice1712_playback_pro =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
/*	formats:	  */	SNDRV_PCM_FMTBIT_S32_LE,
/*	rates:		  */	SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_96000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	96000,
/*	channels_min:	  */	10,
/*	channels_max:	  */	10,
/*	buffer_bytes_max:  */	(256*1024),
/*	period_bytes_min:  */	10 * 4 * 2,
/*	period_bytes_max:  */	131040,
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

static snd_pcm_hardware_t snd_ice1712_capture_pro =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
			  	 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
/*	formats:	  */	SNDRV_PCM_FMTBIT_S32_LE,
/*	rates:		  */	SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_96000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	96000,
/*	channels_min:	  */	12,
/*	channels_max:	  */	12,
/*	buffer_bytes_max:  */	(256*1024),
/*	period_bytes_min:  */	12 * 4 * 2,
/*	period_bytes_max:  */	131040,
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};
#else
static snd_pcm_hardware_t snd_ice1712_playback_pro =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
	formats:		SNDRV_PCM_FMTBIT_S32_LE,
	rates:			SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_96000,
	rate_min:		4000,
	rate_max:		96000,
	channels_min:		10,
	channels_max:		10,
	buffer_bytes_max:	(256*1024),
	period_bytes_min:	10 * 4 * 2,
	period_bytes_max:	131040,
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_ice1712_capture_pro =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
	formats:		SNDRV_PCM_FMTBIT_S32_LE,
	rates:			SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_96000,
	rate_min:		4000,
	rate_max:		96000,
	channels_min:		12,
	channels_max:		12,
	buffer_bytes_max:	(256*1024),
	period_bytes_min:	12 * 4 * 2,
	period_bytes_max:	131040,
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};
#endif

static int snd_ice1712_playback_pro_open(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	ice1712_t *ice = snd_pcm_substream_chip(substream);

	ice->playback_pro_substream = substream;
	runtime->hw = snd_ice1712_playback_pro;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);

	ice->cs8403_spdif_stream_bits = ice->cs8403_spdif_bits;
	memcpy(ice->cs8427_spdif_stream_status, ice->cs8427_spdif_status, 5);
	if (ice->spdif_stream_ctl != NULL) {
		ice->spdif_stream_ctl->access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		snd_ctl_notify(ice->card, SNDRV_CTL_EVENT_MASK_VALUE |
			       SNDRV_CTL_EVENT_MASK_INFO, &ice->spdif_stream_ctl->id);
	}

	return 0;
}

static int snd_ice1712_capture_pro_open(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	ice->capture_pro_substream = substream;
	runtime->hw = snd_ice1712_capture_pro;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);
	return 0;
}

static int snd_ice1712_playback_pro_close(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);

	ice->playback_pro_substream = NULL;

	if (ice->spdif_stream_ctl != NULL) {
		ice->spdif_stream_ctl->access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		snd_ctl_notify(ice->card, SNDRV_CTL_EVENT_MASK_VALUE |
			       SNDRV_CTL_EVENT_MASK_INFO, &ice->spdif_stream_ctl->id);
	}

	return 0;
}

static int snd_ice1712_capture_pro_close(snd_pcm_substream_t * substream)
{
	ice1712_t *ice = snd_pcm_substream_chip(substream);

	ice->capture_pro_substream = NULL;
	return 0;
}

static void __exit snd_ice1712_pcm_profi_free(snd_pcm_t *pcm)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, pcm->private_data, return);
	ice->pcm_pro = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_ice1712_playback_pro_ops = {
	snd_ice1712_playback_pro_open,
	snd_ice1712_playback_pro_close,
	snd_pcm_lib_ioctl,
	snd_ice1712_hw_params,
	snd_ice1712_hw_free,
	snd_ice1712_playback_pro_prepare,
	snd_ice1712_pro_trigger,
	snd_ice1712_playback_pro_pointer,0,0
};

static snd_pcm_ops_t snd_ice1712_capture_pro_ops = {
	snd_ice1712_capture_pro_open,
	snd_ice1712_capture_pro_close,
	snd_pcm_lib_ioctl,
	snd_ice1712_hw_params,
	snd_ice1712_hw_free,
	snd_ice1712_capture_pro_prepare,
	snd_ice1712_pro_trigger,
	snd_ice1712_capture_pro_pointer,0,0
};
#else
static snd_pcm_ops_t snd_ice1712_playback_pro_ops = {
	open:		snd_ice1712_playback_pro_open,
	close:		snd_ice1712_playback_pro_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_ice1712_hw_params,
	hw_free:	snd_ice1712_hw_free,
	prepare:	snd_ice1712_playback_pro_prepare,
	trigger:	snd_ice1712_pro_trigger,
	pointer:	snd_ice1712_playback_pro_pointer,
};

static snd_pcm_ops_t snd_ice1712_capture_pro_ops = {
	open:		snd_ice1712_capture_pro_open,
	close:		snd_ice1712_capture_pro_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_ice1712_hw_params,
	hw_free:	snd_ice1712_hw_free,
	prepare:	snd_ice1712_capture_pro_prepare,
	trigger:	snd_ice1712_pro_trigger,
	pointer:	snd_ice1712_capture_pro_pointer,
};
#endif

static int __init snd_ice1712_pcm_profi(ice1712_t * ice, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(ice->card, "ICE1712 multi", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ice1712_playback_pro_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ice1712_capture_pro_ops);

	pcm->private_data = ice;
	pcm->private_free = snd_ice1712_pcm_profi_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, "ICE1712 multi");

	snd_pcm_lib_preallocate_pci_pages_for_all(ice->pci, pcm, 256*1024, 256*1024);

	ice->pcm_pro = pcm;
	if (rpcm)
		*rpcm = pcm;
	
	if ((err = snd_ice1712_build_pro_mixer(ice)) < 0)
		return err;
	return 0;
}

/*
 *  Mixer section
 */

static void snd_ice1712_update_volume(ice1712_t *ice, int index)
{
	unsigned int vol = ice->pro_volumes[index];
	unsigned short val = 0;

	val |= (vol & 0x8000) == 0 ? (96 - (vol & 0x7f)) : 0x7f;
	val |= ((vol & 0x80000000) == 0 ? (96 - ((vol >> 16) & 0x7f)) : 0x7f) << 8;
	outb(index, ICEMT(ice, MONITOR_INDEX));
	outw(val, ICEMT(ice, MONITOR_VOLUME));
}

static int snd_ice1712_pro_mixer_switch_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_pro_mixer_switch_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int index = kcontrol->private_value;
	
	spin_lock_irqsave(&ice->reg_lock, flags);
	ucontrol->value.integer.value[0] = !((ice->pro_volumes[index] >> 15) & 1);
	ucontrol->value.integer.value[1] = !((ice->pro_volumes[index] >> 31) & 1);
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static int snd_ice1712_pro_mixer_switch_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;
	unsigned int nval, change;

	nval = (ucontrol->value.integer.value[0] ? 0 : 0x00008000) |
	       (ucontrol->value.integer.value[1] ? 0 : 0x80000000);
	spin_lock_irqsave(&ice->reg_lock, flags);
	nval |= ice->pro_volumes[index] & ~0x80008000;
	change = nval != ice->pro_volumes[index];
	ice->pro_volumes[index] = nval;
	snd_ice1712_update_volume(ice, index);
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return change;
}

static int snd_ice1712_pro_mixer_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 96;
	return 0;
}

static int snd_ice1712_pro_mixer_volume_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int index = kcontrol->private_value;
	
	spin_lock_irqsave(&ice->reg_lock, flags);
	ucontrol->value.integer.value[0] = (ice->pro_volumes[index] >> 0) & 127;
	ucontrol->value.integer.value[1] = (ice->pro_volumes[index] >> 16) & 127;
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static int snd_ice1712_pro_mixer_volume_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;
	unsigned int nval, change;

	nval = (ucontrol->value.integer.value[0] & 127) |
	       ((ucontrol->value.integer.value[1] & 127) << 16);
	spin_lock_irqsave(&ice->reg_lock, flags);
	nval |= ice->pro_volumes[index] & ~0x007f007f;
	change = nval != ice->pro_volumes[index];
	ice->pro_volumes[index] = nval;
	snd_ice1712_update_volume(ice, index);
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return change;
}

static int snd_ice1712_ak4524_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_ice1712_ak4524_volume_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->private_value / 8;
	int addr = kcontrol->private_value % 8;
	ucontrol->value.integer.value[0] = ice->ak4524_images[chip][addr];
	return 0;
}

static int snd_ice1712_ak4524_volume_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->private_value / 8;
	int addr = kcontrol->private_value % 8;
	unsigned char nval = ucontrol->value.integer.value[0];
	int change = ice->ak4524_images[chip][addr] != nval;
	if (change)
		snd_ice1712_ak4524_write(ice, chip, addr, nval);
	return change;
}

static int snd_ice1712_ak4524_deemphasis_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[4] = {
		"44.1kHz", "Off", "48kHz", "32kHz",
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= 4)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ice1712_ak4524_deemphasis_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->id.index;
	ucontrol->value.enumerated.item[0] = ice->ak4524_images[chip][3] & 3;
	return 0;
}

static int snd_ice1712_ak4524_deemphasis_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int chip = kcontrol->id.index;
	unsigned char nval = ucontrol->value.enumerated.item[0];
	int change;
	nval |= (nval & 3) | (ice->ak4524_images[chip][3] & ~3);
	change = ice->ak4524_images[chip][3] != nval;
	if (change)
		snd_ice1712_ak4524_write(ice, chip, 3, nval);
	return change;
}

static int __init snd_ice1712_build_pro_mixer(ice1712_t *ice)
{
	snd_card_t * card = ice->card;
	snd_kcontrol_t ctl;
	int idx, err;

	/* PCM playback */
	for (idx = 0; idx < 10; idx++) {
		memset(&ctl, 0, sizeof(ctl));
		strcpy(ctl.id.name, "Multi Playback Switch");
		ctl.id.index = idx;
		ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl.info = snd_ice1712_pro_mixer_switch_info;
		ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
		ctl.get = snd_ice1712_pro_mixer_switch_get;
		ctl.put = snd_ice1712_pro_mixer_switch_put;
		ctl.private_value = idx;
		ctl.private_data = ice;
		if ((err = snd_ctl_add(card, snd_ctl_new(&ctl))) < 0)
			return err;
		memset(&ctl, 0, sizeof(ctl));
		strcpy(ctl.id.name, "Multi Playback Volume");
		ctl.id.index = idx;
		ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl.info = snd_ice1712_pro_mixer_volume_info;
		ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
		ctl.get = snd_ice1712_pro_mixer_volume_get;
		ctl.put = snd_ice1712_pro_mixer_volume_put;
		ctl.private_value = idx;
		ctl.private_data = ice;
		if ((err = snd_ctl_add(card, snd_ctl_new(&ctl))) < 0)
			return err;
	}

	/* PCM capture */
	for (idx = 0; idx < 10; idx++) {
		memset(&ctl, 0, sizeof(ctl));
		strcpy(ctl.id.name, "Multi Capture Switch");
		ctl.id.index = idx;
		ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl.info = snd_ice1712_pro_mixer_switch_info;
		ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
		ctl.get = snd_ice1712_pro_mixer_switch_get;
		ctl.put = snd_ice1712_pro_mixer_switch_put;
		ctl.private_value = idx + 10;
		ctl.private_data = ice;
		if ((err = snd_ctl_add(card, snd_ctl_new(&ctl))) < 0)
			return err;
		memset(&ctl, 0, sizeof(ctl));
		strcpy(ctl.id.name, "Multi Capture Volume");
		ctl.id.index = idx;
		ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl.info = snd_ice1712_pro_mixer_volume_info;
		ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
		ctl.get = snd_ice1712_pro_mixer_volume_get;
		ctl.put = snd_ice1712_pro_mixer_volume_put;
		ctl.private_value = idx + 10;
		ctl.private_data = ice;
		if ((err = snd_ctl_add(card, snd_ctl_new(&ctl))) < 0)
			return err;
	}
	
	return 0;
}

static void /*__init*/ snd_ice1712_ac97_init(ac97_t *ac97)
{
	// ice1712_t *ice = snd_magic_cast(ice1712_t, ac97->private_data, return);

        /* disable center DAC/surround DAC/LFE DAC/MIC ADC */
        snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, 0xe800, 0xe800);
}

static void __exit snd_ice1712_mixer_free_ac97(ac97_t *ac97)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, ac97->private_data, return);
	ice->ac97 = NULL;
}

static int __init snd_ice1712_ac97_mixer(ice1712_t * ice)
{
	int err;

	if (!(ice->eeprom.codec & ICE1712_CFG_NO_CON_AC97)) {
		ac97_t ac97;
		memset(&ac97, 0, sizeof(ac97));
		ac97.write = snd_ice1712_ac97_write;
		ac97.read = snd_ice1712_ac97_read;
		ac97.init = snd_ice1712_ac97_init;
		ac97.private_data = ice;
		ac97.private_free = snd_ice1712_mixer_free_ac97;
		if ((err = snd_ac97_mixer(ice->card, &ac97, &ice->ac97)) < 0)
			return err;
		return 0;
	}
	/* hmm.. can we have both consumer and pro ac97 mixers? */
	if (! (ice->eeprom.aclink & ICE1712_CFG_PRO_I2S)) {
		ac97_t ac97;
		memset(&ac97, 0, sizeof(ac97));
		ac97.write = snd_ice1712_pro_ac97_write;
		ac97.read = snd_ice1712_pro_ac97_read;
		ac97.init = snd_ice1712_ac97_init;
		ac97.private_data = ice;
		ac97.private_free = snd_ice1712_mixer_free_ac97;
		if ((err = snd_ac97_mixer(ice->card, &ac97, &ice->ac97)) < 0)
			return err;
		if ((err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_mixer_digmix_route_ac97, ice))) < 0)
			return err;
		return 0;
	}
	/* I2S mixer only */
	strcat(ice->card->mixername, "ICE1712 - multitrack");
	return 0;
}

/*
 *
 */

static void snd_ice1712_proc_read(snd_info_entry_t *entry, 
				  snd_info_buffer_t * buffer)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, entry->private_data, return);
	unsigned int idx;

	snd_iprintf(buffer, "ICE1712\n\n");
	snd_iprintf(buffer, "EEPROM:\n");
	snd_iprintf(buffer, "  Subvendor        : 0x%x\n", ice->eeprom.subvendor);
	snd_iprintf(buffer, "  Size             : %i bytes\n", ice->eeprom.size);
	snd_iprintf(buffer, "  Version          : %i\n", ice->eeprom.version);
	snd_iprintf(buffer, "  Codec            : 0x%x\n", ice->eeprom.codec);
	snd_iprintf(buffer, "  ACLink           : 0x%x\n", ice->eeprom.aclink);
	snd_iprintf(buffer, "  I2S ID           : 0x%x\n", ice->eeprom.i2sID);
	snd_iprintf(buffer, "  S/PDIF           : 0x%x\n", ice->eeprom.spdif);
	snd_iprintf(buffer, "  GPIO mask        : 0x%x\n", ice->eeprom.gpiomask);
	snd_iprintf(buffer, "  GPIO state       : 0x%x\n", ice->eeprom.gpiostate);
	snd_iprintf(buffer, "  GPIO direction   : 0x%x\n", ice->eeprom.gpiodir);
	snd_iprintf(buffer, "  AC'97 main       : 0x%x\n", ice->eeprom.ac97main);
	snd_iprintf(buffer, "  AC'97 pcm        : 0x%x\n", ice->eeprom.ac97pcm);
	snd_iprintf(buffer, "  AC'97 record     : 0x%x\n", ice->eeprom.ac97rec);
	snd_iprintf(buffer, "  AC'97 record src : 0x%x\n", ice->eeprom.ac97recsrc);
	for (idx = 0; idx < 4; idx++)
		snd_iprintf(buffer, "  DAC ID #%i        : 0x%x\n", idx, ice->eeprom.dacID[idx]);
	for (idx = 0; idx < 4; idx++)
		snd_iprintf(buffer, "  ADC ID #%i        : 0x%x\n", idx, ice->eeprom.adcID[idx]);
	for (idx = 0x1c; idx < ice->eeprom.size && idx < 0x1c + sizeof(ice->eeprom.extra); idx++)
		snd_iprintf(buffer, "  Extra #%02i        : 0x%x\n", idx, ice->eeprom.extra[idx - 0x1c]);
}

static void __init snd_ice1712_proc_init(ice1712_t * ice)
{
	snd_info_entry_t *entry;

	if ((entry = snd_info_create_card_entry(ice->card, "ice1712", ice->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = ice;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 2048;
		entry->c.text.read = snd_ice1712_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ice->proc_entry = entry;
}

static void __exit snd_ice1712_proc_done(ice1712_t * ice)
{
	if (ice->proc_entry) {
		snd_info_unregister(ice->proc_entry);
		ice->proc_entry = NULL;
	}
}

/*
 *
 */

static int snd_ice1712_eeprom_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = 32;
	return 0;
}

static int snd_ice1712_eeprom_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	
	memcpy(ucontrol->value.bytes.data, &ice->eeprom, 32);
	return 0;
}

static snd_kcontrol_new_t snd_ice1712_eeprom = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_CARD,0,0,
	"ICE1712 EEPROM",0,
	SNDRV_CTL_ELEM_ACCESS_READ,
	snd_ice1712_eeprom_info,
	snd_ice1712_eeprom_get,0,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_CARD,
	name: "ICE1712 EEPROM",
	access: SNDRV_CTL_ELEM_ACCESS_READ,
	info: snd_ice1712_eeprom_info,
	get: snd_ice1712_eeprom_get
#endif
};

/*
 */
static int snd_ice1712_spdif_default_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ice1712_spdif_default_get(snd_kcontrol_t * kcontrol,
					 snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
		snd_ice1712_decode_cs8403_spdif_bits(&ucontrol->value.iec958, ice->cs8403_spdif_bits);
		break;
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_EWX2496:
		memcpy(ucontrol->value.iec958.status, ice->cs8427_spdif_status, 5);
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		snd_ice1712_ews_decode_cs8404_spdif_bits(&ucontrol->value.iec958, ice->cs8403_spdif_bits);
		break;
	}
	return 0;
}

static int snd_ice1712_spdif_default_put(snd_kcontrol_t * kcontrol,
					 snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;

	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
		val = snd_ice1712_encode_cs8403_spdif_bits(&ucontrol->value.iec958);
		spin_lock_irqsave(&ice->reg_lock, flags);
		change = ice->cs8403_spdif_bits != val;
		ice->cs8403_spdif_bits = val;
		if (change && ice->playback_pro_substream == NULL) {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
			snd_ice1712_delta_cs8403_spdif_write(ice, val);
		} else {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
		}
		break;
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_EWX2496:
		spin_lock_irqsave(&ice->reg_lock, flags);
		change = memcmp(ucontrol->value.iec958.status, ice->cs8427_spdif_status, 5) != 0;
		memcpy(ice->cs8427_spdif_status, ucontrol->value.iec958.status, 5);
		if (change && ice->playback_pro_substream == NULL) {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
			ice->cs8427_ops->write_bytes(ice, 32, 5, ice->cs8427_spdif_status);
		} else
			spin_unlock_irqrestore(&ice->reg_lock, flags);
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		val = snd_ice1712_ews_encode_cs8404_spdif_bits(&ucontrol->value.iec958);
		spin_lock_irqsave(&ice->reg_lock, flags);
		change = ice->cs8403_spdif_bits != val;
		ice->cs8403_spdif_bits = val;
		if (change && ice->playback_pro_substream == NULL) {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
			snd_ice1712_ews_cs8404_spdif_write(ice, val);
		} else {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
		}
		break;
	default:
		change = 0;
	}
	return change;
}

static snd_kcontrol_new_t snd_ice1712_spdif_default =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),0,0,
	snd_ice1712_spdif_default_info,
	snd_ice1712_spdif_default_get,
	snd_ice1712_spdif_default_put,0
#else
	iface:		SNDRV_CTL_ELEM_IFACE_PCM,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	info:		snd_ice1712_spdif_default_info,
	get:		snd_ice1712_spdif_default_get,
	put:		snd_ice1712_spdif_default_put
#endif
};

static int snd_ice1712_spdif_mask_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ice1712_spdif_maskc_get(snd_kcontrol_t * kcontrol,
				       snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);

	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		ucontrol->value.iec958.status[0] = IEC958_AES0_NONAUDIO |
						     IEC958_AES0_PROFESSIONAL |
						     IEC958_AES0_CON_NOT_COPYRIGHT |
						     IEC958_AES0_CON_EMPHASIS;
		ucontrol->value.iec958.status[1] = IEC958_AES1_CON_ORIGINAL |
						     IEC958_AES1_CON_CATEGORY;
		ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS;
		break;
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_EWX2496:
		ucontrol->value.iec958.status[0] = 0xff;
		ucontrol->value.iec958.status[1] = 0xff;
		ucontrol->value.iec958.status[2] = 0xff;
		ucontrol->value.iec958.status[3] = 0xff;
		ucontrol->value.iec958.status[4] = 0xff;
		break;
	}
	return 0;
}

static int snd_ice1712_spdif_maskp_get(snd_kcontrol_t * kcontrol,
				       snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);

	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		ucontrol->value.iec958.status[0] = IEC958_AES0_NONAUDIO |
						     IEC958_AES0_PROFESSIONAL |
						     IEC958_AES0_PRO_FS |
						     IEC958_AES0_PRO_EMPHASIS;
		ucontrol->value.iec958.status[1] = IEC958_AES1_PRO_MODE;
		break;
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_EWX2496:
		ucontrol->value.iec958.status[0] = 0xff;
		ucontrol->value.iec958.status[1] = 0xff;
		ucontrol->value.iec958.status[2] = 0xff;
		ucontrol->value.iec958.status[3] = 0xff;
		ucontrol->value.iec958.status[4] = 0xff;
		break;
	}
	return 0;
}

static snd_kcontrol_new_t snd_ice1712_spdif_maskc =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),0,
	SNDRV_CTL_ELEM_ACCESS_READ,
	snd_ice1712_spdif_mask_info,
	snd_ice1712_spdif_maskc_get,0,0
#else
	access:		SNDRV_CTL_ELEM_ACCESS_READ,
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	info:		snd_ice1712_spdif_mask_info,
	get:		snd_ice1712_spdif_maskc_get,
#endif
};

static snd_kcontrol_new_t snd_ice1712_spdif_maskp =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),0,
	SNDRV_CTL_ELEM_ACCESS_READ,
	snd_ice1712_spdif_mask_info,
	snd_ice1712_spdif_maskp_get,0,0
#else
	access:		SNDRV_CTL_ELEM_ACCESS_READ,
	iface:		SNDRV_CTL_ELEM_IFACE_MIXER,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
	info:		snd_ice1712_spdif_mask_info,
	get:		snd_ice1712_spdif_maskp_get,
#endif
};

static int snd_ice1712_spdif_stream_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ice1712_spdif_stream_get(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);

	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
		snd_ice1712_decode_cs8403_spdif_bits(&ucontrol->value.iec958, ice->cs8403_spdif_stream_bits);
		break;
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_EWX2496:
		//ice->cs8427_ops->read_bytes(ice, 32, 5, ucontrol->value.iec958.status);
		memcpy(ucontrol->value.iec958.status, ice->cs8427_spdif_stream_status, 5);
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		snd_ice1712_ews_decode_cs8404_spdif_bits(&ucontrol->value.iec958, ice->cs8403_spdif_stream_bits);
		break;
	}
	return 0;
}

static int snd_ice1712_spdif_stream_put(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;

	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
		val = snd_ice1712_encode_cs8403_spdif_bits(&ucontrol->value.iec958);
		spin_lock_irqsave(&ice->reg_lock, flags);
		change = ice->cs8403_spdif_stream_bits != val;
		ice->cs8403_spdif_stream_bits = val;
		if (change && ice->playback_pro_substream != NULL) {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
			snd_ice1712_delta_cs8403_spdif_write(ice, val);
		} else {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
		}
		break;
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_EWX2496:
		spin_lock_irqsave(&ice->reg_lock, flags);
		change = memcmp(ice->cs8427_spdif_stream_status,
				ucontrol->value.iec958.status, 5) != 0;
		if (change && ice->playback_pro_substream != NULL) {
			memcpy(ice->cs8427_spdif_stream_status,
			       ucontrol->value.iec958.status, 5);
			spin_unlock_irqrestore(&ice->reg_lock, flags);
			ice->cs8427_ops->write_bytes(ice, 32, 5, ucontrol->value.iec958.status);
		} else
			spin_unlock_irqrestore(&ice->reg_lock, flags);
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		val = snd_ice1712_ews_encode_cs8404_spdif_bits(&ucontrol->value.iec958);
		spin_lock_irqsave(&ice->reg_lock, flags);
		change = ice->cs8403_spdif_stream_bits != val;
		ice->cs8403_spdif_stream_bits = val;
		if (change && ice->playback_pro_substream != NULL) {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
			snd_ice1712_ews_cs8404_spdif_write(ice, val);
		} else {
			spin_unlock_irqrestore(&ice->reg_lock, flags);
		}
		break;
	default:
		change = 0;
	}
	return change;
}

static snd_kcontrol_new_t snd_ice1712_spdif_stream =
{
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_PCM,0,0,
	SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),0,
	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	snd_ice1712_spdif_stream_info,
	snd_ice1712_spdif_stream_get,
	snd_ice1712_spdif_stream_put,0
#else
	access:		SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	iface:		SNDRV_CTL_ELEM_IFACE_PCM,
	name:           SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	info:		snd_ice1712_spdif_stream_info,
	get:		snd_ice1712_spdif_stream_get,
	put:		snd_ice1712_spdif_stream_put
#endif
};

#ifdef TARGET_OS2
#define ICE1712_GPIO(xiface, xname, xindex, shift, xaccess) \
{ xiface, 0, 0, xname, 0, xaccess, snd_ice1712_gpio_info, \
  snd_ice1712_gpio_get, snd_ice1712_gpio_put, \
  shift }
#else
#define ICE1712_GPIO(xiface, xname, xindex, shift, xaccess) \
{ iface: xiface, name: xname, access: xaccess, info: snd_ice1712_gpio_info, \
  get: snd_ice1712_gpio_get, put: snd_ice1712_gpio_put, \
  private_value: shift }
#endif

static int snd_ice1712_gpio_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_gpio_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char mask = kcontrol->private_value & 0xff;
	unsigned char saved[2];
	
	save_gpio_status(ice, saved);
	ucontrol->value.integer.value[0] = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & mask ? 1 : 0;
	restore_gpio_status(ice, saved);
	return 0;
}

static int snd_ice1712_gpio_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char mask = kcontrol->private_value & 0xff;
	unsigned char saved[2];
	int val, nval;

	if (kcontrol->private_value & (1 << 31))
		return -EPERM;
	nval = ucontrol->value.integer.value[0] ? mask : 0;
	save_gpio_status(ice, saved);
	val = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	nval |= val & ~mask;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, nval);
	restore_gpio_status(ice, saved);
	return val != nval;
}

static snd_kcontrol_new_t snd_ice1712_delta1010_wordclock_select =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_PCM, "Word Clock Sync", 0, ICE1712_DELTA_WORD_CLOCK_SELECT, 0);
static snd_kcontrol_new_t snd_ice1712_delta1010_wordclock_status =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_PCM, "Word Clock Status", 0, ICE1712_DELTA_WORD_CLOCK_STATUS, SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE);
static snd_kcontrol_new_t snd_ice1712_deltadio2496_spdif_in_select =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_PCM, "IEC958 Input Optical", 0, ICE1712_DELTA_SPDIF_INPUT_SELECT, 0);
static snd_kcontrol_new_t snd_ice1712_delta_spdif_in_status =
ICE1712_GPIO(SNDRV_CTL_ELEM_IFACE_PCM, "Delta IEC958 Input Status", 0, ICE1712_DELTA_SPDIF_IN_STAT, SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE);

static int snd_ice1712_pro_spdif_master_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_pro_spdif_master_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&ice->reg_lock, flags);
	ucontrol->value.integer.value[0] = inb(ICEMT(ice, RATE)) & ICE1712_SPDIF_MASTER ? 1 : 0;
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static int snd_ice1712_pro_spdif_master_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int nval, change;

	nval = ucontrol->value.integer.value[0] ? ICE1712_SPDIF_MASTER : 0;
	spin_lock_irqsave(&ice->reg_lock, flags);
	nval |= inb(ICEMT(ice, RATE)) & ~ICE1712_SPDIF_MASTER;
	change = inb(ICEMT(ice, RATE)) != nval;
	outb(nval, ICEMT(ice, RATE));
	spin_unlock_irqrestore(&ice->reg_lock, flags);

	if (ice->cs8427_ops) {
		/* change CS8427 clock source too */
		snd_ice1712_cs8427_set_input_clock(ice, ucontrol->value.integer.value[0]);
	}

	return change;
}

static snd_kcontrol_new_t snd_ice1712_pro_spdif_master = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Multi Track IEC958 Master",0,0,
	snd_ice1712_pro_spdif_master_info,
	snd_ice1712_pro_spdif_master_get,
	snd_ice1712_pro_spdif_master_put,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Multi Track IEC958 Master",
	info: snd_ice1712_pro_spdif_master_info,
	get: snd_ice1712_pro_spdif_master_get,
	put: snd_ice1712_pro_spdif_master_put
#endif
};

/*
 * routing
 */
static int snd_ice1712_pro_route_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[] = {
		"PCM Out", /* 0 */
		"H/W In 0", "H/W In 1", "H/W In 2", "H/W In 3", /* 1-4 */
		"H/W In 4", "H/W In 5", "H/W In 6", "H/W In 7", /* 5-8 */
		"IEC958 In L", "IEC958 In R", /* 9-10 */
		"Digital Mixer", /* 11 - optional */
	};
	
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = kcontrol->id.index < 2 ? 12 : 11;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ice1712_pro_route_analog_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->id.index;
	unsigned int val, cval;
	val = inw(ICEMT(ice, ROUTE_PSDOUT03));
	val >>= ((idx % 2) * 8) + ((idx / 2) * 2);
	val &= 3;
	cval = inw(ICEMT(ice, ROUTE_CAPTURE));
	cval >>= ((idx / 2) * 8) + ((idx % 2) * 4);
	if (val == 1 && idx < 2)
		ucontrol->value.enumerated.item[0] = 11;
	else if (val == 2)
		ucontrol->value.enumerated.item[0] = (cval & 7) + 1;
	else if (val == 3)
		ucontrol->value.enumerated.item[0] = ((cval >> 3) & 1) + 9;
	else
		ucontrol->value.enumerated.item[0] = 0;
	return 0;
}

static int snd_ice1712_pro_route_analog_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int change, shift;
	int idx = kcontrol->id.index;
	unsigned int val, old_val, nval;
	
	/* update PSDOUT */
	if (ucontrol->value.enumerated.item[0] >= 11)
		nval = idx < 2 ? 1 : 0;
	else if (ucontrol->value.enumerated.item[0] >= 9)
		nval = 3;
	else if (ucontrol->value.enumerated.item[0] >= 1)
		nval = 2;
	else
		nval = 0;
	shift = ((idx % 2) * 8) + ((idx / 2) * 2);
	val = old_val = inw(ICEMT(ice, ROUTE_PSDOUT03));
	val &= ~(0x03 << shift);
	val |= nval << shift;
	change = val != old_val;
	if (change)
		outw(val, ICEMT(ice, ROUTE_PSDOUT03));
	if (nval < 2)
		return change;

	/* update CAPTURE */
	val = old_val = inw(ICEMT(ice, ROUTE_CAPTURE));
	shift = ((idx / 2) * 8) + ((idx % 2) * 4);
	if (nval == 2) {
		nval = ucontrol->value.enumerated.item[0] - 1;
		val &= ~(0x07 << shift);
		val |= nval << shift;
	} else {
		nval = (ucontrol->value.enumerated.item[0] - 9) << 3;
		val &= ~(0x08 << shift);
		val |= nval << shift;
	}
	if (val != old_val) {
		change = 1;
		outw(val, ICEMT(ice, ROUTE_CAPTURE));
	}
	return change;
}

static int snd_ice1712_pro_route_spdif_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int idx = kcontrol->id.index;
	unsigned int val, cval;
	val = inw(ICEMT(ice, ROUTE_SPDOUT));
	cval = (val >> (idx * 4 + 8)) & 0x0f;
	val = (val >> (idx * 2)) & 0x03;
	if (val == 1)
		ucontrol->value.enumerated.item[0] = 11;
	else if (val == 2)
		ucontrol->value.enumerated.item[0] = (cval & 7) + 1;
	else if (val == 3)
		ucontrol->value.enumerated.item[0] = ((cval >> 3) & 1) + 9;
	else
		ucontrol->value.enumerated.item[0] = 0;
	return 0;
}

static int snd_ice1712_pro_route_spdif_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int change, shift;
	int idx = kcontrol->id.index;
	unsigned int val, old_val, nval;
	
	/* update SPDOUT */
	val = old_val = inw(ICEMT(ice, ROUTE_SPDOUT));
	if (ucontrol->value.enumerated.item[0] >= 11)
		nval = 1;
	else if (ucontrol->value.enumerated.item[0] >= 9)
		nval = 3;
	else if (ucontrol->value.enumerated.item[0] >= 1)
		nval = 2;
	else
		nval = 0;
	shift = idx * 2;
	val &= ~(0x03 << shift);
	val |= nval << shift;
	shift = idx * 4 + 8;
	if (nval == 2) {
		nval = ucontrol->value.enumerated.item[0] - 1;
		val &= ~(0x07 << shift);
		val |= nval << shift;
	} else if (nval == 3) {
		nval = (ucontrol->value.enumerated.item[0] - 9) << 3;
		val &= ~(0x08 << shift);
		val |= nval << shift;
	}
	change = val != old_val;
	if (change)
		outw(val, ICEMT(ice, ROUTE_SPDOUT));
	return change;
}

static snd_kcontrol_new_t snd_ice1712_mixer_pro_analog_route = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"H/W Playback Route",0,0,
	snd_ice1712_pro_route_info,
	snd_ice1712_pro_route_analog_get,
	snd_ice1712_pro_route_analog_put,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "H/W Playback Route",
	info: snd_ice1712_pro_route_info,
	get: snd_ice1712_pro_route_analog_get,
	put: snd_ice1712_pro_route_analog_put,
#endif
};

static snd_kcontrol_new_t snd_ice1712_mixer_pro_spdif_route = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"IEC958 Playback Route",0,0,
	snd_ice1712_pro_route_info,
	snd_ice1712_pro_route_spdif_get,
	snd_ice1712_pro_route_spdif_put,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "IEC958 Playback Route",
	info: snd_ice1712_pro_route_info,
	get: snd_ice1712_pro_route_spdif_get,
	put: snd_ice1712_pro_route_spdif_put,
#endif
};


static int snd_ice1712_pro_volume_rate_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_ice1712_pro_volume_rate_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&ice->reg_lock, flags);
	ucontrol->value.integer.value[0] = inb(ICEMT(ice, MONITOR_RATE));
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static int snd_ice1712_pro_volume_rate_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int change;

	spin_lock_irqsave(&ice->reg_lock, flags);
	change = inb(ICEMT(ice, MONITOR_RATE)) != ucontrol->value.integer.value[0];
	outb(ucontrol->value.integer.value[0], ICEMT(ice, MONITOR_RATE));
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_ice1712_mixer_pro_volume_rate = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Multi Track Volume Rate",0,0,
	snd_ice1712_pro_volume_rate_info,
	snd_ice1712_pro_volume_rate_get,
	snd_ice1712_pro_volume_rate_put,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Multi Track Volume Rate",
	info: snd_ice1712_pro_volume_rate_info,
	get: snd_ice1712_pro_volume_rate_get,
	put: snd_ice1712_pro_volume_rate_put
#endif
};

static int snd_ice1712_pro_peak_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 22;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_ice1712_pro_peak_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int idx;
	
	spin_lock_irqsave(&ice->reg_lock, flags);
	for (idx = 0; idx < 22; idx++) {
		outb(idx, ICEMT(ice, MONITOR_PEAKINDEX));
		ucontrol->value.integer.value[idx] = inb(ICEMT(ice, MONITOR_PEAKDATA));
	}
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	return 0;
}

static snd_kcontrol_new_t snd_ice1712_mixer_pro_peak = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Multi Track Peak",0,
	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	snd_ice1712_pro_peak_info,
	snd_ice1712_pro_peak_get,0,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Multi Track Peak",
	access: SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	info: snd_ice1712_pro_peak_info,
	get: snd_ice1712_pro_peak_get
#endif
};

/*
 * EWX 24/96
 */

static int snd_ice1712_ewx_io_sense_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo){

	static char *texts[4] = {
		"+4dBu", "-10dBV",
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= 2)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ice1712_ewx_io_sense_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char mask = kcontrol->private_value & 0xff;
	unsigned char saved[2];
	
	save_gpio_status(ice, saved);
	ucontrol->value.enumerated.item[0] = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & mask ? 1 : 0;
	restore_gpio_status(ice, saved);
	return 0;
}

static int snd_ice1712_ewx_io_sense_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char mask = kcontrol->private_value & 0xff;
	unsigned char saved[2];
	int val, nval;

	if (kcontrol->private_value & (1 << 31))
		return -EPERM;
	nval = ucontrol->value.enumerated.item[0] ? mask : 0;
	save_gpio_status(ice, saved);
	val = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	nval |= val & ~mask;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, nval);
	restore_gpio_status(ice, saved);
	return val != nval;
}

static snd_kcontrol_new_t snd_ice1712_ewx_input_sense = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Input Sensitivity Switch",0,0,
	snd_ice1712_ewx_io_sense_info,
	snd_ice1712_ewx_io_sense_get,
	snd_ice1712_ewx_io_sense_put,
	ICE1712_EWX2496_AIN_SEL,
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Input Sensitivity Switch",
	info: snd_ice1712_ewx_io_sense_info,
	get: snd_ice1712_ewx_io_sense_get,
	put: snd_ice1712_ewx_io_sense_put,
	private_value: ICE1712_EWX2496_AIN_SEL,
#endif
};

static snd_kcontrol_new_t snd_ice1712_ewx_output_sense = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Output Sensitivity Switch",0,0,
	snd_ice1712_ewx_io_sense_info,
	snd_ice1712_ewx_io_sense_get,
	snd_ice1712_ewx_io_sense_put,
	ICE1712_EWX2496_AOUT_SEL,
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Output Sensitivity Switch",
	info: snd_ice1712_ewx_io_sense_info,
	get: snd_ice1712_ewx_io_sense_get,
	put: snd_ice1712_ewx_io_sense_put,
	private_value: ICE1712_EWX2496_AOUT_SEL,
#endif
};


/*
 * EWS88MT
 */
/* analog output sensitivity;; address 0x48 bit 6 */
static int snd_ice1712_ews88mt_output_sense_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char saved[2];
	unsigned char data;

	save_gpio_status(ice, saved);
	data = snd_ice1712_ews88mt_pcf8574_read(ice, ICE1712_EWS88MT_OUTPUT_ADDR);
	restore_gpio_status(ice, saved);
	ucontrol->value.enumerated.item[0] = data & ICE1712_EWS88MT_OUTPUT_SENSE ? 1 : 0; /* high = -10dBV, low = +4dBu */
	return 0;
}

/* analog output sensitivity;; address 0x48 bit 6 */
static int snd_ice1712_ews88mt_output_sense_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char saved[2];
	unsigned char data, ndata;

	save_gpio_status(ice, saved);
	data = snd_ice1712_ews88mt_pcf8574_read(ice, ICE1712_EWS88MT_OUTPUT_ADDR);
	ndata = (data & ~ICE1712_EWS88MT_OUTPUT_SENSE) | (ucontrol->value.enumerated.item[0] ? ICE1712_EWS88MT_OUTPUT_SENSE : 0);
	if (ndata != data)
		snd_ice1712_ews88mt_pcf8574_write(ice, ICE1712_EWS88MT_OUTPUT_ADDR, ndata);
	restore_gpio_status(ice, saved);
	return ndata != data;

}

/* analog input sensitivity; address 0x46 */
static int snd_ice1712_ews88mt_input_sense_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int channel = kcontrol->id.index;
	unsigned char saved[2];
	unsigned char data;

	snd_assert(channel >= 0 && channel <= 7, return 0);
	save_gpio_status(ice, saved);
	data = snd_ice1712_ews88mt_pcf8574_read(ice, ICE1712_EWS88MT_INPUT_ADDR);
	restore_gpio_status(ice, saved);
	/* reversed; high = +4dBu, low = -10dBV */
	ucontrol->value.enumerated.item[0] = data & (1 << channel) ? 0 : 1;
	return 0;
}

/* analog output sensitivity; address 0x46 */
static int snd_ice1712_ews88mt_input_sense_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int channel = kcontrol->id.index;
	unsigned char saved[2];
	unsigned char data, ndata;

	snd_assert(channel >= 0 && channel <= 7, return 0);
	save_gpio_status(ice, saved);
	data = snd_ice1712_ews88mt_pcf8574_read(ice, ICE1712_EWS88MT_INPUT_ADDR);
	ndata = (data & ~(1 << channel)) | (ucontrol->value.enumerated.item[0] ? 0 : (1 << channel));
	if (ndata != data)
		snd_ice1712_ews88mt_pcf8574_write(ice, ICE1712_EWS88MT_INPUT_ADDR, ndata);
	restore_gpio_status(ice, saved);
	return ndata != data;
}

static snd_kcontrol_new_t snd_ice1712_ews88mt_input_sense = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Input Sensitivity Switch",0,0,
	snd_ice1712_ewx_io_sense_info,
	snd_ice1712_ews88mt_input_sense_get,
	snd_ice1712_ews88mt_input_sense_put,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Input Sensitivity Switch",
	info: snd_ice1712_ewx_io_sense_info,
	get: snd_ice1712_ews88mt_input_sense_get,
	put: snd_ice1712_ews88mt_input_sense_put,
#endif
};

static snd_kcontrol_new_t snd_ice1712_ews88mt_output_sense = {
#ifdef TARGET_OS2
	SNDRV_CTL_ELEM_IFACE_MIXER,0,0,
	"Output Sensitivity Switch",0,0,
	snd_ice1712_ewx_io_sense_info,
	snd_ice1712_ews88mt_output_sense_get,
	snd_ice1712_ews88mt_output_sense_put,0
#else
	iface: SNDRV_CTL_ELEM_IFACE_MIXER,
	name: "Output Sensitivity Switch",
	info: snd_ice1712_ewx_io_sense_info,
	get: snd_ice1712_ews88mt_output_sense_get,
	put: snd_ice1712_ews88mt_output_sense_put,
#endif
};


/*
 * EWS88D controls
 */

static int snd_ice1712_ews88d_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_ews88d_control_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value >> 8) & 1;
	unsigned short data;
	
	data = snd_ice1712_ews88d_pcf8575_read(ice, ICE1712_EWS88D_PCF_ADDR);
	//printk("pcf: read 0x%x\n", data);
	data = (data >> shift) & 0x01;
	if (invert)
		data ^= 0x01;
	ucontrol->value.integer.value[0] = data;
	return 0;
}

static int snd_ice1712_ews88d_control_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value >> 8) & 1;
	unsigned short data, ndata;
	int change;

	data = snd_ice1712_ews88d_pcf8575_read(ice, ICE1712_EWS88D_PCF_ADDR);
	ndata = data & ~(1 << shift);
	if (invert) {
		if (! ucontrol->value.integer.value[0])
			ndata |= (1 << shift);
	} else {
		if (ucontrol->value.integer.value[0])
			ndata |= (1 << shift);
	}
	change = (data != ndata);
	if (change) {
		ndata &= 0xff;
		ndata |= (unsigned short)ice->cs8403_spdif_stream_bits << 8;
		snd_ice1712_ews88d_pcf8575_write(ice, ICE1712_EWS88D_PCF_ADDR, ndata);
		//printk("pcf: write 0x%x\n", ndata);
	}
	return change;
}

#ifdef TARGET_OS2
#define EWS88D_CONTROL(xiface, xname, xindex, xshift, xinvert, xaccess) \
{ xiface,0,0,\
  xname, 0,\
  xaccess,\
  snd_ice1712_ews88d_control_info,\
  snd_ice1712_ews88d_control_get,\
  snd_ice1712_ews88d_control_put,\
  xshift | (xinvert << 8),\
}
#else
#define EWS88D_CONTROL(xiface, xname, xindex, xshift, xinvert, xaccess) \
{ iface: xiface,\
  name: xname,\
  access: xaccess,\
  info: snd_ice1712_ews88d_control_info,\
  get: snd_ice1712_ews88d_control_get,\
  put: snd_ice1712_ews88d_control_put,\
  private_value: xshift | (xinvert << 8),\
}
#endif

static snd_kcontrol_new_t snd_ice1712_ews88d_spdif_in_opt =
EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "IEC958 Input Optical", 0, 0, 1, 0); /* inverted */
static snd_kcontrol_new_t snd_ice1712_ews88d_opt_out_adat =
EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "ADAT Output Optical", 0, 1, 0, 0);
static snd_kcontrol_new_t snd_ice1712_ews88d_master_adat =
EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "ADAT External Master Clock", 0, 2, 0, 0);
static snd_kcontrol_new_t snd_ice1712_ews88d_adat_enable =
EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "Enable ADAT", 0, 3, 0, 0);
static snd_kcontrol_new_t snd_ice1712_ews88d_adat_through =
EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "ADAT Through", 0, 4, 1, 0);


/*
 *
 */

static unsigned char __init snd_ice1712_read_i2c(ice1712_t *ice,
						 unsigned char dev,
						 unsigned char addr)
{
	long t = 0x10000;

	outb(addr, ICEREG(ice, I2C_BYTE_ADDR));
	outb(dev & ~ICE1712_I2C_WRITE, ICEREG(ice, I2C_DEV_ADDR));
	while (t-- > 0 && (inb(ICEREG(ice, I2C_CTRL)) & ICE1712_I2C_BUSY)) ;
	return inb(ICEREG(ice, I2C_DATA));
}

static int __init snd_ice1712_read_eeprom(ice1712_t *ice)
{
	int dev = 0xa0;		/* EEPROM device address */
	unsigned int idx;

	if ((inb(ICEREG(ice, I2C_CTRL)) & ICE1712_I2C_EEPROM) == 0) {
		snd_printk("ICE1712 has not detected EEPROM\n");
		return -EIO;
	}
	ice->eeprom.subvendor = (snd_ice1712_read_i2c(ice, dev, 0x00) << 0) |
				(snd_ice1712_read_i2c(ice, dev, 0x01) << 8) | 
				(snd_ice1712_read_i2c(ice, dev, 0x02) << 16) | 
				(snd_ice1712_read_i2c(ice, dev, 0x03) << 24);
	ice->eeprom.size = snd_ice1712_read_i2c(ice, dev, 0x04);
	if (ice->eeprom.size < 28) {
		snd_printk("invalid EEPROM (size = %i)\n", ice->eeprom.size);
		return -EIO;
	}
	ice->eeprom.version = snd_ice1712_read_i2c(ice, dev, 0x05);
	if (ice->eeprom.version != 1) {
		snd_printk("invalid EEPROM version %i\n", ice->eeprom.version);
		return -EIO;
	}
	ice->eeprom.codec = snd_ice1712_read_i2c(ice, dev, 0x06);
	ice->eeprom.aclink = snd_ice1712_read_i2c(ice, dev, 0x07);
	ice->eeprom.i2sID = snd_ice1712_read_i2c(ice, dev, 0x08);
	ice->eeprom.spdif = snd_ice1712_read_i2c(ice, dev, 0x09);
	ice->eeprom.gpiomask = snd_ice1712_read_i2c(ice, dev, 0x0a);
	ice->eeprom.gpiostate = snd_ice1712_read_i2c(ice, dev, 0x0b);
	ice->eeprom.gpiodir = snd_ice1712_read_i2c(ice, dev, 0x0c);
	ice->eeprom.ac97main = (snd_ice1712_read_i2c(ice, dev, 0x0d) << 0) |
			       (snd_ice1712_read_i2c(ice, dev, 0x0e) << 8);
	ice->eeprom.ac97pcm = (snd_ice1712_read_i2c(ice, dev, 0x0f) << 0) |
			      (snd_ice1712_read_i2c(ice, dev, 0x10) << 8);
	ice->eeprom.ac97rec = (snd_ice1712_read_i2c(ice, dev, 0x11) << 0) |
			      (snd_ice1712_read_i2c(ice, dev, 0x12) << 8);
	ice->eeprom.ac97recsrc = snd_ice1712_read_i2c(ice, dev, 0x13) << 0;
	for (idx = 0; idx < 4; idx++) {
		ice->eeprom.dacID[idx] = snd_ice1712_read_i2c(ice, dev, 0x14 + idx);
		ice->eeprom.adcID[idx] = snd_ice1712_read_i2c(ice, dev, 0x18 + idx);
	}
	for (idx = 0x1c; idx < ice->eeprom.size && idx < 0x1c + sizeof(ice->eeprom.extra); idx++)
		ice->eeprom.extra[idx - 0x1c] = snd_ice1712_read_i2c(ice, dev, idx);
	return 0;
}

static void __init snd_ice1712_ak4524_init(ice1712_t *ice)
{
	static unsigned char inits[8] = {
		0x07, /* 0: all power up */
		0x03, /* 1: ADC/DAC reset */
		0x60, /* 2: 24bit I2S */
		0x19, /* 3: deemphasis off */
		0x00, /* 4: ADC left muted */
		0x00, /* 5: ADC right muted */
		0x00, /* 6: DAC left muted */
		0x00, /* 7: DAC right muted */
	};
	int chip;
	unsigned char reg;

	for (chip = 0; chip < ice->num_dacs/2; chip++) {
		for (reg = 0; reg < 8; ++reg)
			snd_ice1712_ak4524_write(ice, chip, reg, inits[reg]);
	}
}

/* init CS8427 transciever */
static void __init snd_ice1712_cs8427_init(ice1712_t *ice)
{
	static unsigned char initvals[] = {
		/* RMCK to OMCK, no validity, disable mutes, TCBL=output */
		0x80,
		/* hold last valid audio sample, RMCK=256*Fs, normal stereo operation */
		0x00,
		/* output drivers normal operation, Tx<=serial, Rx=>serial */
		0x0c,
		/* Run off, CMCK=256*Fs, output time base = OMCK, input time base =
		   covered input clock, recovered input clock source is Envy24 */
		0x04,
		/* Serial audio input port data format = I2S */
		0x05, /* SIDEL=1, SILRPOL=1 */
		/* Serial audio output port data format = I2S */
		0x05,	/* SODEL=1, SOLRPOL=1 */
	};
	unsigned char buf[32];

	/* verify CS8427 ID */
	if (ice->cs8427_ops->read(ice, 127) != 0x71) {
		snd_printk("unable to find CS8427 signature, initialization is not completed\n");
		return;
	}
	/* turn off run bit while making changes to configuration */
	ice->cs8427_ops->write(ice, 4, 0x00);
	/* send initial values */
	ice->cs8427_ops->write_bytes(ice, 1, 6, initvals);
	/* Turn off CS8427 interrupt stuff that is not used in hardware */
	memset(buf, 0, sizeof(buf));
	/* from address 9 to 15 */
	ice->cs8427_ops->write_bytes(ice, 9, 7, buf);
	/* unmask the input PLL clock, V, confidence, biphase, parity status bits */
	//buf[0] = 0x1f;
	buf[0] = 0;
	/* Registers 32-55 window to CS buffer
	   Inhibit D->E transfers from overwriting first 5 bytes of CS data.
	   Allow D->E transfers (all) of CS data.
	   Allow E->F transfer of CS data.
	   One byte mode; both A/B channels get same written CB data.
	   A channel info is output to chip's EMPH* pin. */
	buf[1] = 0x10;
	/* Use internal buffer to transmit User (U) data.
	   Chip's U pin is an output.
	   Transmit all O's for user data. */
	//buf[2] = 0x10;
	buf[2] = 0x00;
	ice->cs8427_ops->write_bytes(ice, 17, 3, buf);
	/* turn on run bit and rock'n'roll */
	ice->cs8427_ops->write(ice, 4, initvals[3] | 0x40);
	/* write default channel status bytes */
	ice->cs8427_ops->write_bytes(ice, 32, 5, ice->cs8427_spdif_status);

	/*
	 * add controls..
	 */
	snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_cs8427_in_status, ice));
#if 0
	snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_cs8427_clock_select, ice));
#endif
}

static void __init snd_ice1712_stdsp24_gpio_write(ice1712_t *ice, unsigned char byte)
{
	byte |= ICE1712_STDSP24_CLOCK_BIT;
	udelay(100);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, byte);
	byte &= ~ICE1712_STDSP24_CLOCK_BIT;
	udelay(100);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, byte);
	byte |= ICE1712_STDSP24_CLOCK_BIT;
	udelay(100);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, byte);
}

static void __init snd_ice1712_stdsp24_darear(ice1712_t *ice, int activate)
{
	down(&ice->gpio_mutex);
	ICE1712_STDSP24_0_DAREAR(ice->hoontech_boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[0]);
	up(&ice->gpio_mutex);
}

static void __init snd_ice1712_stdsp24_mute(ice1712_t *ice, int activate)
{
	down(&ice->gpio_mutex);
	ICE1712_STDSP24_3_MUTE(ice->hoontech_boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[3]);
	up(&ice->gpio_mutex);
}

static void __init snd_ice1712_stdsp24_insel(ice1712_t *ice, int activate)
{
	down(&ice->gpio_mutex);
	ICE1712_STDSP24_3_INSEL(ice->hoontech_boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[3]);
	up(&ice->gpio_mutex);
}

static void __init snd_ice1712_stdsp24_box_channel(ice1712_t *ice, int box, int chn, int activate)
{
	down(&ice->gpio_mutex);

	/* select box */
	ICE1712_STDSP24_0_BOX(ice->hoontech_boxbits, box);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[0]);

	/* prepare for write */
	if (chn == 3)
		ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 0);
	ICE1712_STDSP24_2_MIDI1(ice->hoontech_boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);

	ICE1712_STDSP24_1_CHN1(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN2(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN3(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 1);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[1]);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	udelay(100);
	if (chn == 3) {
		ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 0);
		snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	} else {
		switch (chn) {
		case 0:	ICE1712_STDSP24_1_CHN1(ice->hoontech_boxbits, 0); break;
		case 1:	ICE1712_STDSP24_1_CHN2(ice->hoontech_boxbits, 0); break;
		case 2:	ICE1712_STDSP24_1_CHN3(ice->hoontech_boxbits, 0); break;
		}
		snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[1]);
	}
	udelay(100);
	ICE1712_STDSP24_1_CHN1(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN2(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN3(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 1);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[1]);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	udelay(100);

	ICE1712_STDSP24_2_MIDI1(ice->hoontech_boxbits, 0);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);

	up(&ice->gpio_mutex);
}

static void __init snd_ice1712_stdsp24_box_midi(ice1712_t *ice, int box, int master, int slave)
{
	down(&ice->gpio_mutex);

	/* select box */
	ICE1712_STDSP24_0_BOX(ice->hoontech_boxbits, box);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[0]);

	ICE1712_STDSP24_2_MIDIIN(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_MIDI1(ice->hoontech_boxbits, master);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);

	udelay(100);
	
	ICE1712_STDSP24_2_MIDIIN(ice->hoontech_boxbits, 0);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);
	
	udelay(100);
	
	ICE1712_STDSP24_2_MIDIIN(ice->hoontech_boxbits, 1);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[2]);

	udelay(100);

	/* MIDI2 is direct */
	ICE1712_STDSP24_3_MIDI2(ice->hoontech_boxbits, slave);
	snd_ice1712_stdsp24_gpio_write(ice, ice->hoontech_boxbits[3]);

	up(&ice->gpio_mutex);
}

static void __init snd_ice1712_stdsp24_init(ice1712_t *ice)
{
	int box, chn;

	ice->hoontech_boxbits[0] = 
	ice->hoontech_boxbits[1] = 
	ice->hoontech_boxbits[2] = 
	ice->hoontech_boxbits[3] = 0;	/* should be already */

	ICE1712_STDSP24_SET_ADDR(ice->hoontech_boxbits, 0);
	ICE1712_STDSP24_CLOCK(ice->hoontech_boxbits, 0, 1);
	ICE1712_STDSP24_0_BOX(ice->hoontech_boxbits, 0);
	ICE1712_STDSP24_0_DAREAR(ice->hoontech_boxbits, 0);

	ICE1712_STDSP24_SET_ADDR(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_CLOCK(ice->hoontech_boxbits, 1, 1);
	ICE1712_STDSP24_1_CHN1(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN2(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_1_CHN3(ice->hoontech_boxbits, 1);
	
	ICE1712_STDSP24_SET_ADDR(ice->hoontech_boxbits, 2);
	ICE1712_STDSP24_CLOCK(ice->hoontech_boxbits, 2, 1);
	ICE1712_STDSP24_2_CHN4(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_MIDIIN(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_2_MIDI1(ice->hoontech_boxbits, 0);

	ICE1712_STDSP24_SET_ADDR(ice->hoontech_boxbits, 3);
	ICE1712_STDSP24_CLOCK(ice->hoontech_boxbits, 3, 1);
	ICE1712_STDSP24_3_MIDI2(ice->hoontech_boxbits, 0);
	ICE1712_STDSP24_3_MUTE(ice->hoontech_boxbits, 1);
	ICE1712_STDSP24_3_INSEL(ice->hoontech_boxbits, 0);

	/* let's go - activate only functions in first box */
	ice->hoontech_config = 0;
			    /* ICE1712_STDSP24_MUTE |
			       ICE1712_STDSP24_INSEL |
			       ICE1712_STDSP24_DAREAR; */
	ice->hoontech_boxconfig[0] = ICE1712_STDSP24_BOX_CHN1 |
				     ICE1712_STDSP24_BOX_CHN2 |
				     ICE1712_STDSP24_BOX_CHN3 |
				     ICE1712_STDSP24_BOX_CHN4 |
				     ICE1712_STDSP24_BOX_MIDI1 |
				     ICE1712_STDSP24_BOX_MIDI2;
	ice->hoontech_boxconfig[1] = 
	ice->hoontech_boxconfig[2] = 
	ice->hoontech_boxconfig[3] = 0;
	snd_ice1712_stdsp24_darear(ice, (ice->hoontech_config & ICE1712_STDSP24_DAREAR) ? 1 : 0);
	snd_ice1712_stdsp24_mute(ice, (ice->hoontech_config & ICE1712_STDSP24_MUTE) ? 1 : 0);
	snd_ice1712_stdsp24_insel(ice, (ice->hoontech_config & ICE1712_STDSP24_INSEL) ? 1 : 0);
	for (box = 0; box < 4; box++) {
		for (chn = 0; chn < 4; chn++)
			snd_ice1712_stdsp24_box_channel(ice, box, chn, (ice->hoontech_boxconfig[box] & (1 << chn)) ? 1 : 0);
		snd_ice1712_stdsp24_box_midi(ice, box,
				(ice->hoontech_boxconfig[box] & ICE1712_STDSP24_BOX_MIDI1) ? 1 : 0,
				(ice->hoontech_boxconfig[box] & ICE1712_STDSP24_BOX_MIDI2) ? 1 : 0);
	}
}

static int __init snd_ice1712_chip_init(ice1712_t *ice)
{
	outb(ICE1712_RESET | ICE1712_NATIVE, ICEREG(ice, CONTROL));
	udelay(200);
	outb(ICE1712_NATIVE, ICEREG(ice, CONTROL));
	udelay(200);
	pci_write_config_byte(ice->pci, 0x60, ice->eeprom.codec);
	pci_write_config_byte(ice->pci, 0x61, ice->eeprom.aclink);
	pci_write_config_byte(ice->pci, 0x62, ice->eeprom.i2sID);
	pci_write_config_byte(ice->pci, 0x63, ice->eeprom.spdif);
	if (ice->eeprom.subvendor != ICE1712_SUBDEVICE_STDSP24) {
		ice->gpio_write_mask = ice->eeprom.gpiomask;
		ice->gpio_direction = ice->eeprom.gpiodir;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ice->eeprom.gpiomask);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, ice->eeprom.gpiodir);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, ice->eeprom.gpiostate);
	} else {
		ice->gpio_write_mask = 0xc0;
		ice->gpio_direction = 0xff;
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, 0xc0);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, 0xff);
		snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, ICE1712_STDSP24_CLOCK_BIT);
	}
	snd_ice1712_write(ice, ICE1712_IREG_PRO_POWERDOWN, 0);
	if (!(ice->eeprom.codec & ICE1712_CFG_NO_CON_AC97)) {
		outb(ICE1712_AC97_WARM, ICEREG(ice, AC97_CMD));
		udelay(100);
		outb(0, ICEREG(ice, AC97_CMD));
		udelay(200);
		snd_ice1712_write(ice, ICE1712_IREG_CONSUMER_POWERDOWN, 0);
	}

	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_EWX2496:
		ice->num_adcs = ice->num_dacs = 2;
		break;	
	case ICE1712_SUBDEVICE_DELTA44:
	case ICE1712_SUBDEVICE_DELTA66:
		ice->num_adcs = ice->num_dacs = 4;
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
		ice->num_adcs = ice->num_dacs = 8;
		break;
	}

	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_DELTA44:
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_EWX2496:
	case ICE1712_SUBDEVICE_EWS88MT:
		snd_ice1712_ak4524_init(ice);
		break;
	case ICE1712_SUBDEVICE_STDSP24:
		snd_ice1712_stdsp24_init(ice);
		break;
	}
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		ice->cs8427_ops = &snd_ice1712_ap_cs8427_ops;
		snd_ice1712_cs8427_init(ice);
		break;
	case ICE1712_SUBDEVICE_EWX2496:
		ice->cs8427_ops = &snd_ice1712_ewx_cs8427_ops;
		snd_ice1712_cs8427_init(ice);
		break;
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
		/* Set spdif defaults */
		snd_ice1712_delta_cs8403_spdif_write(ice, ice->cs8403_spdif_bits);
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		/* Set spdif defaults */
		snd_ice1712_ews_cs8404_spdif_write(ice, ice->cs8403_spdif_bits);
		break;
	}
	return 0;
}

static int __init snd_ice1712_build_controls(ice1712_t *ice)
{
	unsigned int idx;
	snd_kcontrol_t *kctl;
	int err;

	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_eeprom, ice));
	if (err < 0)
		return err;
	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_pro_spdif_master, ice));
	if (err < 0)
		return err;
	for (idx = 0; idx < ice->num_dacs; idx++) {
		kctl = snd_ctl_new1(&snd_ice1712_mixer_pro_analog_route, ice);
		if (kctl == NULL)
			return -ENOMEM;
		kctl->id.index = idx;
		err = snd_ctl_add(ice->card, kctl);
		if (err < 0)
			return err;
	}
	for (idx = 0; idx < 2; idx++) {
		kctl = snd_ctl_new1(&snd_ice1712_mixer_pro_spdif_route, ice);
		if (kctl == NULL)
			return -ENOMEM;
		kctl->id.index = idx;
		err = snd_ctl_add(ice->card, kctl);
		if (err < 0)
			return err;
	}
	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_mixer_pro_volume_rate, ice));
	if (err < 0)
		return err;
	err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_mixer_pro_peak, ice));
	if (err < 0)
		return err;
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta1010_wordclock_select, ice));
		if (err < 0)
			return err;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta1010_wordclock_status, ice));
		if (err < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_DELTADIO2496:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_deltadio2496_spdif_in_select, ice));
		if (err < 0)
			return err;
		break;
	}
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_EWX2496:
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		snd_assert(ice->pcm_pro != NULL, return -EIO);
		err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_ice1712_spdif_default, ice));
		if (err < 0)
			return err;
		kctl->id.device = ice->pcm_pro->device;
		err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_ice1712_spdif_maskc, ice));
		if (err < 0)
			return err;
		kctl->id.device = ice->pcm_pro->device;
		err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_ice1712_spdif_maskp, ice));
		if (err < 0)
			return err;
		kctl->id.device = ice->pcm_pro->device;
		err = snd_ctl_add(ice->card, kctl = snd_ctl_new1(&snd_ice1712_spdif_stream, ice));
		if (err < 0)
			return err;
		kctl->id.device = ice->pcm_pro->device;
		ice->spdif_stream_ctl = kctl;
		break;
	}
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DELTA1010:
	case ICE1712_SUBDEVICE_DELTADIO2496:
	case ICE1712_SUBDEVICE_DELTA66:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_delta_spdif_in_status, ice));
		if (err < 0)
			return err;
		break;
	}
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWX2496:
	case ICE1712_SUBDEVICE_AUDIOPHILE:
	case ICE1712_SUBDEVICE_DELTA44:
	case ICE1712_SUBDEVICE_DELTA66:
	case ICE1712_SUBDEVICE_EWS88MT:
		for (idx = 0; idx < ice->num_dacs; ++idx) {
			snd_kcontrol_t ctl;
			memset(&ctl, 0, sizeof(ctl));
			strcpy(ctl.id.name, "DAC Volume");
			ctl.id.index = idx;
			ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
			ctl.info = snd_ice1712_ak4524_volume_info;
			ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
			ctl.get = snd_ice1712_ak4524_volume_get;
			ctl.put = snd_ice1712_ak4524_volume_put;
			ctl.private_value = (idx / 2) * 8 + (idx % 2) + 6; /* reigster 6 & 7 */
			ctl.private_data = ice;
			if ((err = snd_ctl_add(ice->card, snd_ctl_new(&ctl))) < 0)
				return err;
		}
		for (idx = 0; idx < ice->num_adcs; ++idx) {
			snd_kcontrol_t ctl;
			memset(&ctl, 0, sizeof(ctl));
			strcpy(ctl.id.name, "ADC Volume");
			ctl.id.index = idx;
			ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
			ctl.info = snd_ice1712_ak4524_volume_info;
			ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
			ctl.get = snd_ice1712_ak4524_volume_get;
			ctl.put = snd_ice1712_ak4524_volume_put;
			ctl.private_value = (idx / 2) * 8 + (idx % 2) + 4; /* reigster 4 & 5 */
			ctl.private_data = ice;
			if ((err = snd_ctl_add(ice->card, snd_ctl_new(&ctl))) < 0)
				return err;
		}
		for (idx = 0; idx < ice->num_dacs/2; idx++) {
			snd_kcontrol_t ctl;
			memset(&ctl, 0, sizeof(ctl));
			strcpy(ctl.id.name, "Deemphasis");
			ctl.id.index = idx;
			ctl.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
			ctl.info = snd_ice1712_ak4524_deemphasis_info;
			ctl.access = SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE;
			ctl.get = snd_ice1712_ak4524_deemphasis_get;
			ctl.put = snd_ice1712_ak4524_deemphasis_put;
			ctl.private_data = ice;
			if ((err = snd_ctl_add(ice->card, snd_ctl_new(&ctl))) < 0)
				return err;
		}
		break;
	}
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWX2496:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ewx_input_sense, ice));
		if (err < 0)
			return err;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ewx_output_sense, ice));
		if (err < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
		for (idx = 0; idx < 8; idx++) {
			kctl = snd_ctl_new1(&snd_ice1712_ews88mt_input_sense, ice);
			kctl->id.index = idx;
			err = snd_ctl_add(ice->card, kctl);
			if (err < 0)
				return err;
		}
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ews88mt_output_sense, ice));
		if (err < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_EWS88D:
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ews88d_spdif_in_opt, ice));
		if (err < 0)
			return err;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ews88d_opt_out_adat, ice));
		if (err < 0)
			return err;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ews88d_master_adat, ice));
		if (err < 0)
			return err;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ews88d_adat_enable, ice));
		if (err < 0)
			return err;
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ews88d_adat_through, ice));
		if (err < 0)
			return err;
		break;
	}

	return 0;
}

static int __exit snd_ice1712_free(ice1712_t *ice)
{
	if (ice->res_port == NULL)
		goto __hw_end;
	/* mask all interrupts */
	outb(0xc0, ICEMT(ice, IRQ));
	outb(0xff, ICEREG(ice, IRQMASK));
	/* --- */
      __hw_end:
	snd_ice1712_proc_done(ice);
	synchronize_irq();
	if (ice->irq)
		free_irq(ice->irq, (void *) ice);
	if (ice->res_port)
		release_resource(ice->res_port);
	if (ice->res_ddma_port)
		release_resource(ice->res_ddma_port);
	if (ice->res_dmapath_port)
		release_resource(ice->res_dmapath_port);
	if (ice->res_profi_port)
		release_resource(ice->res_profi_port);
	snd_magic_kfree(ice);
	return 0;
}

static int __exit snd_ice1712_dev_free(snd_device_t *device)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, device->device_data, return -ENXIO);
	return snd_ice1712_free(ice);
}

static int __init snd_ice1712_create(snd_card_t * card,
				     struct pci_dev *pci,
				     ice1712_t ** r_ice1712)
{
	ice1712_t *ice;
	int err;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_ice1712_dev_free,0,0
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_ice1712_dev_free,
	};
#endif
	*r_ice1712 = NULL;

        /* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	/* check, if we can restrict PCI DMA transfers to 28 bits */
	if (!pci_dma_supported(pci, 0x0fffffff)) {
		snd_printk("architecture does not support 28bit PCI busmaster DMA\n");
		return -ENXIO;
	}
	pci_set_dma_mask(pci, 0x0fffffff);

	ice = snd_magic_kcalloc(ice1712_t, 0, GFP_KERNEL);
	if (ice == NULL)
		return -ENOMEM;
	spin_lock_init(&ice->reg_lock);
	init_MUTEX(&ice->gpio_mutex);
	ice->cs8403_spdif_bits =
	ice->cs8403_spdif_stream_bits = (0x01 |	/* consumer format */
					 0x10 |	/* no emphasis */
					 0x20);	/* PCM encoder/decoder */
	ice->cs8427_spdif_status[0] = (SNDRV_PCM_DEFAULT_CON_SPDIF >> 0) & 0xff;
	ice->cs8427_spdif_status[1] = (SNDRV_PCM_DEFAULT_CON_SPDIF >> 8) & 0xff;
	ice->cs8427_spdif_status[2] = (SNDRV_PCM_DEFAULT_CON_SPDIF >> 16) & 0xff;
	ice->cs8427_spdif_status[3] = (SNDRV_PCM_DEFAULT_CON_SPDIF >> 24) & 0xff;
	memcpy(ice->cs8427_spdif_stream_status, ice->cs8427_spdif_status, 5);
	ice->card = card;
	ice->pci = pci;
	ice->irq = -1;
	ice->port = pci_resource_start(pci, 0);
	ice->ddma_port = pci_resource_start(pci, 1);
	ice->dmapath_port = pci_resource_start(pci, 2);
	ice->profi_port = pci_resource_start(pci, 3);
	pci_set_master(pci);
	pci_write_config_word(ice->pci, 0x40, 0x807f);
	pci_write_config_word(ice->pci, 0x42, 0x0006);
	snd_ice1712_proc_init(ice);
	synchronize_irq();

	if ((ice->res_port = request_region(ice->port, 32, "ICE1712 - Controller")) == NULL) {
		snd_ice1712_free(ice);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", ice->port, ice->port + 32 - 1);
		return -EIO;
	}
	if ((ice->res_ddma_port = request_region(ice->ddma_port, 16, "ICE1712 - DDMA")) == NULL) {
		snd_ice1712_free(ice);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", ice->ddma_port, ice->ddma_port + 16 - 1);
		return -EIO;
	}
	if ((ice->res_dmapath_port = request_region(ice->dmapath_port, 16, "ICE1712 - DMA path")) == NULL) {
		snd_ice1712_free(ice);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", ice->dmapath_port, ice->dmapath_port + 16 - 1);
		return -EIO;
	}
	if ((ice->res_profi_port = request_region(ice->profi_port, 64, "ICE1712 - Professional")) == NULL) {
		snd_ice1712_free(ice);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", ice->profi_port, ice->profi_port + 16 - 1);
		return -EIO;
	}
	if (request_irq(pci->irq, snd_ice1712_interrupt, SA_INTERRUPT|SA_SHIRQ, "ICE1712", (void *) ice)) {
		snd_ice1712_free(ice);
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		return -EIO;
	}
	ice->irq = pci->irq;

	if (snd_ice1712_read_eeprom(ice) < 0) {
		snd_ice1712_free(ice);
		return -EIO;
	}
	if (snd_ice1712_chip_init(ice) < 0) {
		snd_ice1712_free(ice);
		return -EIO;
	}

	/* unmask used interrupts */
	outb((ice->eeprom.codec & ICE1712_CFG_2xMPU401) == 0 ? ICE1712_IRQ_MPU2 : 0 |
	     (ice->eeprom.codec & ICE1712_CFG_NO_CON_AC97) ? ICE1712_IRQ_PBKDS | ICE1712_IRQ_CONCAP | ICE1712_IRQ_CONPBK : 0,
	     ICEREG(ice, IRQMASK));
	outb(0x00, ICEMT(ice, IRQ));

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, ice, &ops)) < 0) {
		snd_ice1712_free(ice);
		return err;
	}

	*r_ice1712 = ice;
	return 0;
}

static int __init snd_ice1712_probe(struct pci_dev *pci,
				    const struct pci_device_id *id)
{
	static int dev = 0;
	snd_card_t *card;
	ice1712_t *ice;
	int pcm_dev = 0, err;

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

	if ((err = snd_ice1712_create(card, pci, &ice)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_ice1712_pcm_profi(ice, pcm_dev++, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	
	if (!(ice->eeprom.codec & ICE1712_CFG_NO_CON_AC97))
		if ((err = snd_ice1712_pcm(ice, pcm_dev++, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}

	if ((err = snd_ice1712_ac97_mixer(ice)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_ice1712_build_controls(ice)) < 0) {
		snd_card_free(card);
		return err;
	}

	if (!(ice->eeprom.codec & ICE1712_CFG_NO_CON_AC97))
		if ((err = snd_ice1712_pcm_ds(ice, pcm_dev++, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}

	strcpy(card->driver, "ICE1712");
	strcpy(card->shortname, "ICEnsemble ICE1712");
	
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_STDSP24:
		strcpy(card->shortname, "Hoontech SoundTrack Audio DSP24");
		break;
	case ICE1712_SUBDEVICE_DELTA1010:
		strcpy(card->shortname, "M Audio Delta 1010");
		break;
	case ICE1712_SUBDEVICE_DELTADIO2496:
		strcpy(card->shortname, "M Audio Delta DiO 2496");
		goto __no_mpu401;
	case ICE1712_SUBDEVICE_DELTA66:
		strcpy(card->shortname, "M Audio Delta 66");
		goto __no_mpu401;
	case ICE1712_SUBDEVICE_DELTA44:
		strcpy(card->shortname, "M Audio Delta 44");
		goto __no_mpu401;
	case ICE1712_SUBDEVICE_AUDIOPHILE:
		strcpy(card->shortname, "M Audio Audiophile 24/96");
		break;
	case ICE1712_SUBDEVICE_EWX2496:
		strcpy(card->shortname, "TerraTec EWX 24/96");
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
		strcpy(card->shortname, "TerraTec EWS 88MT");
		break;
	case ICE1712_SUBDEVICE_EWS88D:
		strcpy(card->shortname, "TerraTec EWS 88D");
		break;
	}

	if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_ICE1712,
				       ICEREG(ice, MPU1_CTRL), 1,
				       ice->irq, 0,
				       &ice->rmidi[0])) < 0) {
		snd_card_free(card);
		return err;
	}

	if (ice->eeprom.codec & ICE1712_CFG_2xMPU401)
		if ((err = snd_mpu401_uart_new(card, 1, MPU401_HW_ICE1712,
					       ICEREG(ice, MPU2_CTRL), 1,
					       ice->irq, 0,
					       &ice->rmidi[1])) < 0) {
			snd_card_free(card);
			return err;
		}

      __no_mpu401:
	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, ice->port, ice->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	PCI_SET_DRIVER_DATA(pci, card);
	dev++;
	return 0;
}

static void __exit snd_ice1712_remove(struct pci_dev *pci)
{
	snd_card_free(PCI_GET_DRIVER_DATA(pci));
	PCI_SET_DRIVER_DATA(pci, NULL);
}

#ifdef TARGET_OS2
static struct pci_driver driver = {
	0,0,"ICE1712",
	snd_ice1712_ids,
	snd_ice1712_probe,
	snd_ice1712_remove,0,0
};
#else
static struct pci_driver driver = {
	name: "ICE1712",
	id_table: snd_ice1712_ids,
	probe: snd_ice1712_probe,
	remove: snd_ice1712_remove,
};
#endif

static int __init alsa_card_ice1712_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		snd_printk("ICE1712 soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_ice1712_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_ice1712_init)
module_exit(alsa_card_ice1712_exit)

#ifndef MODULE

/* format is: snd-card-ice1712=snd_enable,snd_index,snd_id */

static int __init alsa_card_ice1712_setup(char *str)
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

__setup("snd-card-ice1712=", alsa_card_ice1712_setup);

#endif /* ifndef MODULE */
