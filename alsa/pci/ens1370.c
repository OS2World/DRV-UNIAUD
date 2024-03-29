/*
 *  Driver for Ensoniq ES1370/ES1371 AudioPCI soundcard
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>,
 *		     Thomas Sailer <sailer@ife.ee.ethz.ch>
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
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/ac97_codec.h>
#include <sound/ak4531_codec.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#define chip_t ensoniq_t

#ifndef CHIP1371
#undef CHIP1370
#define CHIP1370
#endif

EXPORT_NO_SYMBOLS;
#ifdef CHIP1370
MODULE_DESCRIPTION("Ensoniq AudioPCI ES1370");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Ensoniq,AudioPCI-97 ES1370},"
	        "{Creative Labs,SB PCI64/128 (ES1370)}}");
#endif
#ifdef CHIP1371
MODULE_DESCRIPTION("Ensoniq/Creative AudioPCI ES1371+");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Ensoniq,AudioPCI ES1371/73},"
		"{Ensoniq,AudioPCI ES1373},"
		"{Creative Labs,Ectiva EV1938},"
		"{Creative Labs,SB PCI64/128 (ES1371/73)},"
		"{Creative Labs,Vibra PCI128},"
		"{Ectiva,EV1938}}");
#endif

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable switches */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for Ensoniq AudioPCI soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for Ensoniq AudioPCI soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable Ensoniq AudioPCI soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);

#ifndef PCI_DEVICE_ID_ENSONIQ_CT5880
#define PCI_DEVICE_ID_ENSONIQ_CT5880    0x5880
#endif

/* ES1371 chip ID */
/* This is a little confusing because all ES1371 compatible chips have the
   same DEVICE_ID, the only thing differentiating them is the REV_ID field.
   This is only significant if you want to enable features on the later parts.
   Yes, I know it's stupid and why didn't we use the sub IDs?
*/
#define ES1371REV_ES1373_A  0x04
#define ES1371REV_ES1373_B  0x06
#define ES1371REV_CT5880_A  0x07
#define CT5880REV_CT5880_C  0x02
#define CT5880REV_CT5880_D  0x03	/* ??? -jk */
#define ES1371REV_ES1371_B  0x09
#define EV1938REV_EV1938_A  0x00
#define ES1371REV_ES1373_8  0x08

/*
 * Direct registers
 */

#define ES_REG(ensoniq, x) ((ensoniq)->port + ES_REG_##x)

#define ES_REG_CONTROL	0x00	/* R/W: Interrupt/Chip select control register */
#define   ES_1370_ADC_STOP	(1<<31)		/* disable capture buffer transfers */
#define   ES_1370_XCTL1 	(1<<30)		/* general purpose output bit */
#define   ES_1371_SPDIF_EN	(1<<26)		/* SPDIF enable */
#define   ES_1371_JOY_ASEL(o)	(((o)&0x03)<<24)	/* joystick port mapping */
#define   ES_1371_JOY_ASELM	(0x03<<24)	/* mask for above */
#define   ES_1371_JOY_ASELI(i)  (((i)>>24)&0x03)
#define   ES_1371_GPIO_IN(i)	(((i)>>20)&0x0f)	/* GPIO in [3:0] pins - R/O */
#define   ES_1370_PCLKDIVO(o)	(((o)&0x1fff)<<16)	/* clock divide ratio for DAC2 */
#define   ES_1370_PCLKDIVM	((0x1fff)<<16)	/* mask for above */
#define   ES_1370_PCLKDIVI(i)	(((i)>>16)&0x1fff)	/* clock divide ratio for DAC2 */
#define   ES_1371_GPIO_OUT(o)	(((o)&0x0f)<<16)	/* GPIO out [3:0] pins - W/R */
#define   ES_1371_GPIO_OUTM     (0x0f<<16)	/* mask for above */
#define   ES_MSFMTSEL		(1<<15)		/* MPEG serial data format; 0 = SONY, 1 = I2S */
#define   ES_1370_M_SBB		(1<<14)		/* clock source for DAC - 0 = clock generator; 1 = MPEG clocks */
#define   ES_1371_SYNC_RES	(1<<14)		/* Warm AC97 reset */
#define   ES_1370_WTSRSEL(o)	(((o)&0x03)<<12)	/* fixed frequency clock for DAC1 */
#define   ES_1370_WTSRSELM	(0x03<<12)	/* mask for above */
#define   ES_1371_ADC_STOP	(1<<13)		/* disable CCB transfer capture information */
#define   ES_1371_PWR_INTRM	(1<<12)		/* power level change interrupts enable */
#define   ES_1370_DAC_SYNC	(1<<11)		/* DAC's are synchronous */
#define   ES_1371_M_CB		(1<<11)		/* capture clock source; 0 = ADC; 1 = I2S */
#define   ES_CCB_INTRM		(1<<10)		/* CCB voice interrupts enable */
#define   ES_1370_M_CB		(1<<9)	/* capture clock source; 0 = ADC; 1 = MPEG */
#define   ES_1370_XCTL0		(1<<8)	/* generap purpose output bit */
#define   ES_1371_PDLEV(o)	(((o)&0x03)<<8)		/* current power down level */
#define   ES_1371_PDLEVM	(0x03<<8)	/* mask for above */
#define   ES_BREQ		(1<<7)	/* memory bus request enable */
#define   ES_DAC1_EN		(1<<6)	/* DAC1 playback channel enable */
#define   ES_DAC2_EN		(1<<5)	/* DAC2 playback channel enable */
#define   ES_ADC_EN		(1<<4)	/* ADC capture channel enable */
#define   ES_UART_EN		(1<<3)	/* UART enable */
#define   ES_JYSTK_EN		(1<<2)	/* Joystick module enable */
#define   ES_1370_CDC_EN	(1<<1)	/* Codec interface enable */
#define   ES_1371_XTALCKDIS	(1<<1)	/* Xtal clock disable */
#define   ES_1370_SERR_DISABLE	(1<<0)	/* PCI serr signal disable */
#define   ES_1371_PCICLKDIS     (1<<0)	/* PCI clock disable */
#define ES_REG_STATUS	0x04	/* R/O: Interrupt/Chip select status register */
#define   ES_INTR               (1<<31)	/* Interrupt is pending */
#define   ES_1371_ST_AC97_RST	(1<<29) /* CT5880 AC'97 Reset bit */
#define   ES_1371_ST_SPDIF_EN	(1<<18) /* SPDIF enable */
#define   ES_1371_ST_SPDIF_TEST (1<<17) /* SPDIF test */
#define   ES_1371_TEST          (1<<16) /* test ASIC */
#define   ES_1370_CSTAT		(1<<10)	/* CODEC is busy or register write in progress */
#define   ES_1370_CBUSY         (1<<9)	/* CODEC is busy */
#define   ES_1370_CWRIP		(1<<8)	/* CODEC register write in progress */
#define   ES_1371_SYNC_ERR	(1<<8)	/* CODEC synchronization error occured */
#define   ES_1371_VC(i)         (((i)>>6)&0x03)		/* voice code from CCB module */
#define   ES_1370_VC(i)		(((i)>>5)&0x03)		/* voice code from CCB module */
#define   ES_1371_MPWR          (1<<5)	/* power level interrupt pending */
#define   ES_MCCB		(1<<4)	/* CCB interrupt pending */
#define   ES_UART		(1<<3)	/* UART interrupt pending */
#define   ES_DAC1		(1<<2)	/* DAC1 channel interrupt pending */
#define   ES_DAC2		(1<<1)	/* DAC2 channel interrupt pending */
#define   ES_ADC		(1<<0)	/* ADC channel interrupt pending */
#define ES_REG_UART_DATA 0x08	/* R/W: UART data register */
#define ES_REG_UART_STATUS 0x09	/* R/O: UART status register */
#define   ES_RXINT		(1<<7)	/* RX interrupt occured */
#define   ES_TXINT		(1<<2)	/* TX interrupt occured */
#define   ES_TXRDY		(1<<1)	/* transmitter ready */
#define   ES_RXRDY		(1<<0)	/* receiver ready */
#define ES_REG_UART_CONTROL 0x09	/* W/O: UART control register */
#define   ES_RXINTEN		(1<<7)	/* RX interrupt enable */
#define   ES_TXINTENO(o)	(((o)&0x03)<<5)		/* TX interrupt enable */
#define   ES_TXINTENM		(0x03<<5)	/* mask for above */
#define   ES_TXINTENI(i)	(((i)>>5)&0x03)
#define   ES_CNTRL(o)		(((o)&0x03)<<0)		/* control */
#define   ES_CNTRLM		(0x03<<0)	/* mask for above */
#define ES_REG_UART_RES	0x0a	/* R/W: UART reserver register */
#define   ES_TEST_MODE		(1<<0)	/* test mode enabled */
#define ES_REG_MEM_PAGE	0x0c	/* R/W: Memory page register */
#define   ES_MEM_PAGEO(o)	(((o)&0x0f)<<0)		/* memory page select - out */
#define   ES_MEM_PAGEM		(0x0f<<0)	/* mask for above */
#define   ES_MEM_PAGEI(i)       (((i)>>0)&0x0f)		/* memory page select - in */

#define ES_REG_1370_CODEC 0x10	/* W/O: Codec write register address */
#define   ES_1370_CODEC_WRITE(a,d) ((((a)&0xff)<<8)|(((d)&0xff)<<0))
#define ES_REG_1371_CODEC 0x14	/* W/R: Codec Read/Write register address */
#define   ES_1371_CODEC_RDY	   (1<<31)	/* codec ready */
#define   ES_1371_CODEC_WIP	   (1<<30)	/* codec register access in progress */
#define   ES_1371_CODEC_PIRD	   (1<<23)	/* codec read/write select register */
#define   ES_1371_CODEC_WRITE(a,d) ((((a)&0x7f)<<16)|(((d)&0xffff)<<0))
#define   ES_1371_CODEC_READS(a)   ((((a)&0x7f)<<16)|ES_1371_CODEC_PIRD)
#define   ES_1371_CODEC_READ(i)    (((i)>>0)&0xffff)

#define ES_REG_1371_SMPRATE 0x10	/* W/R: Codec rate converter interface register */
#define   ES_1371_SRC_RAM_ADDRO(o) (((o)&0x7f)<<25)	/* address of the sample rate converter */
#define   ES_1371_SRC_RAM_ADDRM	   (0x7f<<25)	/* mask for above */
#define   ES_1371_SRC_RAM_ADDRI(i) (((i)>>25)&0x7f)	/* address of the sample rate converter */
#define   ES_1371_SRC_RAM_WE	   (1<<24)	/* R/W: read/write control for sample rate converter */
#define   ES_1371_SRC_RAM_BUSY     (1<<23)	/* R/O: sample rate memory is busy */
#define   ES_1371_SRC_DISABLE      (1<<22)	/* sample rate converter disable */
#define   ES_1371_DIS_P1	   (1<<21)	/* playback channel 1 accumulator update disable */
#define   ES_1371_DIS_P2	   (1<<20)	/* playback channel 1 accumulator update disable */
#define   ES_1371_DIS_R1	   (1<<19)	/* capture channel accumulator update disable */
#define   ES_1371_SRC_RAM_DATAO(o) (((o)&0xffff)<<0)	/* current value of the sample rate converter */
#define   ES_1371_SRC_RAM_DATAM	   (0xffff<<0)	/* mask for above */
#define   ES_1371_SRC_RAM_DATAI(i) (((i)>>0)&0xffff)	/* current value of the sample rate converter */

#define ES_REG_1371_LEGACY 0x18	/* W/R: Legacy control/status register */
#define   ES_1371_JFAST		(1<<31)		/* fast joystick timing */
#define   ES_1371_HIB		(1<<30)		/* host interrupt blocking enable */
#define   ES_1371_VSB		(1<<29)		/* SB; 0 = addr 0x220xH, 1 = 0x22FxH */
#define   ES_1371_VMPUO(o)	(((o)&0x03)<<27)	/* base register address; 0 = 0x320xH; 1 = 0x330xH; 2 = 0x340xH; 3 = 0x350xH */
#define   ES_1371_VMPUM		(0x03<<27)	/* mask for above */
#define   ES_1371_VMPUI(i)	(((i)>>27)&0x03)	/* base register address */
#define   ES_1371_VCDCO(o)	(((o)&0x03)<<25)	/* CODEC; 0 = 0x530xH; 1 = undefined; 2 = 0xe80xH; 3 = 0xF40xH */
#define   ES_1371_VCDCM		(0x03<<25)	/* mask for above */
#define   ES_1371_VCDCI(i)	(((i)>>25)&0x03)	/* CODEC address */
#define   ES_1371_FIRQ		(1<<24)		/* force an interrupt */
#define   ES_1371_SDMACAP	(1<<23)		/* enable event capture for slave DMA controller */
#define   ES_1371_SPICAP	(1<<22)		/* enable event capture for slave IRQ controller */
#define   ES_1371_MDMACAP	(1<<21)		/* enable event capture for master DMA controller */
#define   ES_1371_MPICAP	(1<<20)		/* enable event capture for master IRQ controller */
#define   ES_1371_ADCAP		(1<<19)		/* enable event capture for ADLIB register; 0x388xH */
#define   ES_1371_SVCAP		(1<<18)		/* enable event capture for SB registers */
#define   ES_1371_CDCCAP	(1<<17)		/* enable event capture for CODEC registers */
#define   ES_1371_BACAP		(1<<16)		/* enable event capture for SoundScape base address */
#define   ES_1371_EXI(i)	(((i)>>8)&0x07)		/* event number */
#define   ES_1371_AI(i)		(((i)>>3)&0x1f)		/* event significant I/O address */
#define   ES_1371_WR		(1<<2)	/* event capture; 0 = read; 1 = write */
#define   ES_1371_LEGINT	(1<<0)	/* interrupt for legacy events; 0 = interrupt did occur */

#define ES_REG_SERIAL	0x20	/* R/W: Serial interface control register */
#define   ES_1371_DAC_TEST	(1<<22)		/* DAC test mode enable */
#define   ES_P2_END_INCO(o)	(((o)&0x07)<<19)	/* binary offset value to increment / loop end */
#define   ES_P2_END_INCM	(0x07<<19)	/* mask for above */
#define   ES_P2_END_INCI(i)	(((i)>>16)&0x07)	/* binary offset value to increment / loop end */
#define   ES_P2_ST_INCO(o)	(((o)&0x07)<<16)	/* binary offset value to increment / start */
#define   ES_P2_ST_INCM		(0x07<<16)	/* mask for above */
#define   ES_P2_ST_INCI(i)	(((i)<<16)&0x07)	/* binary offset value to increment / start */
#define   ES_R1_LOOP_SEL	(1<<15)		/* ADC; 0 - loop mode; 1 = stop mode */
#define   ES_P2_LOOP_SEL	(1<<14)		/* DAC2; 0 - loop mode; 1 = stop mode */
#define   ES_P1_LOOP_SEL	(1<<13)		/* DAC1; 0 - loop mode; 1 = stop mode */
#define   ES_P2_PAUSE		(1<<12)		/* DAC2; 0 - play mode; 1 = pause mode */
#define   ES_P1_PAUSE		(1<<11)		/* DAC1; 0 - play mode; 1 = pause mode */
#define   ES_R1_INT_EN		(1<<10)		/* ADC interrupt enable */
#define   ES_P2_INT_EN		(1<<9)	/* DAC2 interrupt enable */
#define   ES_P1_INT_EN		(1<<8)	/* DAC1 interrupt enable */
#define   ES_P1_SCT_RLD		(1<<7)	/* force sample counter reload for DAC1 */
#define   ES_P2_DAC_SEN		(1<<6)	/* when stop mode: 0 - DAC2 play back zeros; 1 = DAC2 play back last sample */
#define   ES_R1_MODEO(o)	(((o)&0x03)<<4)		/* ADC mode; 0 = 8-bit mono; 1 = 8-bit stereo; 2 = 16-bit mono; 3 = 16-bit stereo */
#define   ES_R1_MODEM		(0x03<<4)	/* mask for above */
#define   ES_R1_MODEI(i)	(((i)>>4)&0x03)
#define   ES_P2_MODEO(o)	(((o)&0x03)<<2)		/* DAC2 mode; -- '' -- */
#define   ES_P2_MODEM		(0x03<<2)	/* mask for above */
#define   ES_P2_MODEI(i)	(((i)>>2)&0x03)
#define   ES_P1_MODEO(o)	(((o)&0x03)<<0)		/* DAC1 mode; -- '' -- */
#define   ES_P1_MODEM		(0x03<<0)	/* mask for above */
#define   ES_P1_MODEI(i)	(((i)>>0)&0x03)

#define ES_REG_DAC1_COUNT 0x24	/* R/W: DAC1 sample count register */
#define ES_REG_DAC2_COUNT 0x28	/* R/W: DAC2 sample count register */
#define ES_REG_ADC_COUNT  0x2c	/* R/W: ADC sample count register */
#define   ES_REG_CURR_COUNT(i)  (((i)>>16)&0xffff)
#define   ES_REG_COUNTO(o)	(((o)&0xffff)<<0)
#define   ES_REG_COUNTM		(0xffff<<0)
#define   ES_REG_COUNTI(i)	(((i)>>0)&0xffff)

#define ES_REG_DAC1_FRAME 0x30	/* R/W: PAGE 0x0c; DAC1 frame address */
#define ES_REG_DAC1_SIZE  0x34	/* R/W: PAGE 0x0c; DAC1 frame size */
#define ES_REG_DAC2_FRAME 0x38	/* R/W: PAGE 0x0c; DAC2 frame address */
#define ES_REG_DAC2_SIZE  0x3c	/* R/W: PAGE 0x0c; DAC2 frame size */
#define ES_REG_ADC_FRAME  0x30	/* R/W: PAGE 0x0d; ADC frame address */
#define ES_REG_ADC_SIZE	  0x34	/* R/W: PAGE 0x0d; ADC frame size */
#define   ES_REG_FCURR_COUNTO(o) (((o)&0xffff)<<16)
#define   ES_REG_FCURR_COUNTM    (0xffff<<16)
#define   ES_REG_FCURR_COUNTI(i) (((i)>>14)&0x3fffc)
#define   ES_REG_FSIZEO(o)	 (((o)&0xffff)<<0)
#define   ES_REG_FSIZEM		 (0xffff<<0)
#define   ES_REG_FSIZEI(i)	 (((i)>>0)&0xffff)
#define ES_REG_PHANTOM_FRAME 0x38 /* R/W: PAGE 0x0d: phantom frame address */
#define ES_REG_PHANTOM_COUNT 0x3c /* R/W: PAGE 0x0d: phantom frame count */

#define ES_REG_UART_FIFO  0x30	/* R/W: PAGE 0x0e; UART FIFO register */
#define   ES_REG_UF_VALID	 (1<<8)
#define   ES_REG_UF_BYTEO(o)	 (((o)&0xff)<<0)
#define   ES_REG_UF_BYTEM	 (0xff<<0)
#define   ES_REG_UF_BYTEI(i)	 (((i)>>0)&0xff)


/*
 *  Pages
 */

#define ES_PAGE_DAC	0x0c
#define ES_PAGE_ADC	0x0d
#define ES_PAGE_UART	0x0e
#define ES_PAGE_UART1	0x0f

/*
 *  Sample rate converter addresses
 */

#define ES_SMPREG_DAC1		0x70
#define ES_SMPREG_DAC2		0x74
#define ES_SMPREG_ADC		0x78
#define ES_SMPREG_VOL_ADC	0x6c
#define ES_SMPREG_VOL_DAC1	0x7c
#define ES_SMPREG_VOL_DAC2	0x7e
#define ES_SMPREG_TRUNC_N	0x00
#define ES_SMPREG_INT_REGS	0x01
#define ES_SMPREG_ACCUM_FRAC	0x02
#define ES_SMPREG_VFREQ_FRAC	0x03

/*
 *  Some contants
 */

#define ES_1370_SRCLOCK	   1411200
#define ES_1370_SRTODIV(x) (ES_1370_SRCLOCK/(x)-2)

/*
 *  Open modes
 */

#define ES_MODE_PLAY1	0x0001
#define ES_MODE_PLAY2	0x0002
#define ES_MODE_CAPTURE	0x0004

#define ES_MODE_OUTPUT	0x0001	/* for MIDI */
#define ES_MODE_INPUT	0x0002	/* for MIDI */

/*

 */

typedef struct _snd_ensoniq ensoniq_t;

struct _snd_ensoniq {
	spinlock_t reg_lock;

	int irq;

	unsigned long playback1size;
	unsigned long playback2size;
	unsigned long capture3size;

	unsigned long port;
	struct resource *res_port;
	unsigned int mode;
	unsigned int uartm;	/* UART mode */

	unsigned int ctrl;	/* control register */
	unsigned int sctrl;	/* serial control register */
	unsigned int cssr;	/* control status register */
	unsigned int uartc;	/* uart control register */
	unsigned int rev;	/* chip revision */

	union {
		struct {
			ac97_t *ac97;
		} es1371;
		struct {
			int pclkdiv_lock;
			ak4531_t *ak4531;
		} es1370;
	} u;

	struct pci_dev *pci;
	unsigned short subsystem_vendor_id;
	unsigned short subsystem_device_id;
	snd_card_t *card;
	snd_pcm_t *pcm1;	/* DAC1/ADC PCM */
	snd_pcm_t *pcm2;	/* DAC2 PCM */
	snd_pcm_substream_t *playback1_substream;
	snd_pcm_substream_t *playback2_substream;
	snd_pcm_substream_t *capture_substream;
	unsigned int p1_dma_size;
	unsigned int p2_dma_size;
	unsigned int c_dma_size;
	unsigned int p1_period_size;
	unsigned int p2_period_size;
	unsigned int c_period_size;
	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *midi_input;
	snd_rawmidi_substream_t *midi_output;

	snd_info_entry_t *proc_entry;

#ifdef CHIP1370
	unsigned char *bugbuf;
	dma_addr_t bugbuf_addr;
#endif
};

static void snd_audiopci_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static struct pci_device_id snd_audiopci_ids[] __devinitdata = {
#ifdef CHIP1370
	{ 0x1274, 0x5000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* ES1370 */
#endif
#ifdef CHIP1371
	{ 0x1274, 0x1371, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* ES1371 */
	{ 0x1274, 0x5880, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* ES1373 - CT5880 */
	{ 0x1102, 0x8938, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* Ectiva EV1938 */
#endif
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_audiopci_ids);

/*
 *  constants
 */

#define POLL_COUNT	0xa000

#ifdef CHIP1370
static unsigned int snd_es1370_fixed_rates[] =
	{5512, 11025, 22050, 44100};
#ifdef TARGET_OS2
static snd_pcm_hw_constraint_list_t snd_es1370_hw_constraints_rates = {
	4, 
	snd_es1370_fixed_rates,
	0,
};
static ratnum_t es1370_clock = {
	ES_1370_SRCLOCK,
	29, 
	353,
	1,
};
static snd_pcm_hw_constraint_ratnums_t snd_es1370_hw_constraints_clock = {
	1,
	&es1370_clock,
};
#else
static snd_pcm_hw_constraint_list_t snd_es1370_hw_constraints_rates = {
	count: 4, 
	list: snd_es1370_fixed_rates,
	mask: 0,
};
static ratnum_t es1370_clock = {
	num: ES_1370_SRCLOCK,
	den_min: 29, 
	den_max: 353,
	den_step: 1,
};
static snd_pcm_hw_constraint_ratnums_t snd_es1370_hw_constraints_clock = {
	nrats: 1,
	rats: &es1370_clock,
};
#endif
#else
#ifdef TARGET_OS2
static ratden_t es1371_dac_clock = {
	3000 * (1 << 15),
	48000 * (1 << 15),
	3000,
	1 << 15,
};
static snd_pcm_hw_constraint_ratdens_t snd_es1371_hw_constraints_dac_clock = {
	1,
	&es1371_dac_clock,
};
static ratnum_t es1371_adc_clock = {
	48000 << 15,
	32768, 
	393216,
	1,
};
static snd_pcm_hw_constraint_ratnums_t snd_es1371_hw_constraints_adc_clock = {
	1,
	&es1371_adc_clock,
};
#else
static ratden_t es1371_dac_clock = {
	num_min: 3000 * (1 << 15),
	num_max: 48000 * (1 << 15),
	num_step: 3000,
	den: 1 << 15,
};
static snd_pcm_hw_constraint_ratdens_t snd_es1371_hw_constraints_dac_clock = {
	nrats: 1,
	rats: &es1371_dac_clock,
};
static ratnum_t es1371_adc_clock = {
	num: 48000 << 15,
	den_min: 32768, 
	den_max: 393216,
	den_step: 1,
};
static snd_pcm_hw_constraint_ratnums_t snd_es1371_hw_constraints_adc_clock = {
	nrats: 1,
	rats: &es1371_adc_clock,
};
#endif
#endif
static const unsigned int snd_ensoniq_sample_shift[] =
	{0, 1, 1, 2};

/*
 *  common I/O routines
 */

#ifdef CHIP1371

static unsigned int snd_es1371_wait_src_ready(ensoniq_t * ensoniq)
{
	unsigned int t, r = 0;

	for (t = 0; t < POLL_COUNT; t++) {
		r = inl(ES_REG(ensoniq, 1371_SMPRATE));
		if ((r & ES_1371_SRC_RAM_BUSY) == 0)
			return r;
	}
	snd_printk("wait source ready timeout 0x%lx [0x%x]\n", ES_REG(ensoniq, 1371_SMPRATE), r);
	return 0;
}

static unsigned int snd_es1371_src_read(ensoniq_t * ensoniq, unsigned short reg)
{
	unsigned int temp, i, orig, r;

	/* wait for ready */
	temp = orig = snd_es1371_wait_src_ready(ensoniq);

	/* expose the SRC state bits */
	r = temp & (ES_1371_SRC_DISABLE | ES_1371_DIS_P1 |
		    ES_1371_DIS_P2 | ES_1371_DIS_R1);
	r |= ES_1371_SRC_RAM_ADDRO(reg) | 0x10000;
	outl(r, ES_REG(ensoniq, 1371_SMPRATE));

	/* now, wait for busy and the correct time to read */
	temp = snd_es1371_wait_src_ready(ensoniq);
	
	if ((temp & 0x00870000) != 0x00010000) {
		/* wait for the right state */
		for (i = 0; i < POLL_COUNT; i++) {
			temp = inl(ES_REG(ensoniq, 1371_SMPRATE));
			if ((temp & 0x00870000) == 0x00010000)
				break;
		}
	}

	/* hide the state bits */	
	r = orig & (ES_1371_SRC_DISABLE | ES_1371_DIS_P1 |
		   ES_1371_DIS_P2 | ES_1371_DIS_R1);
	r |= ES_1371_SRC_RAM_ADDRO(reg);
	outl(r, ES_REG(ensoniq, 1371_SMPRATE));
	
	return temp;
}

static void snd_es1371_src_write(ensoniq_t * ensoniq,
				 unsigned short reg, unsigned short data)
{
	unsigned int r;

	r = snd_es1371_wait_src_ready(ensoniq) &
	    (ES_1371_SRC_DISABLE | ES_1371_DIS_P1 |
	     ES_1371_DIS_P2 | ES_1371_DIS_R1);
	r |= ES_1371_SRC_RAM_ADDRO(reg) | ES_1371_SRC_RAM_DATAO(data);
	outl(r | ES_1371_SRC_RAM_WE, ES_REG(ensoniq, 1371_SMPRATE));
}

#endif /* CHIP1371 */

#ifdef CHIP1370

static void snd_es1370_codec_write(ak4531_t *ak4531,
				   unsigned short reg, unsigned short val)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, ak4531->private_data, return);
	unsigned long flags;
	signed long end_time = jiffies + HZ / 10;

#if 0
	printk("CODEC WRITE: reg = 0x%x, val = 0x%x (0x%x), creg = 0x%x\n", reg, val, ES_1370_CODEC_WRITE(reg, val), ES_REG(ensoniq, 1370_CODEC));
#endif
	do {
		spin_lock_irqsave(&ensoniq->reg_lock, flags);
		if (!(inl(ES_REG(ensoniq, STATUS)) & ES_1370_CSTAT)) {
			outw(ES_1370_CODEC_WRITE(reg, val), ES_REG(ensoniq, 1370_CODEC));
			spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
			return;
		}
		spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while ((signed long)(end_time - jiffies) > 0);
	snd_printk("codec write timeout, status = 0x%x\n", inl(ES_REG(ensoniq, STATUS)));
}

#endif /* CHIP1370 */

#ifdef CHIP1371

static void snd_es1371_codec_write(ac97_t *ac97,
				   unsigned short reg, unsigned short val)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, ac97->private_data, return);
	unsigned long flags;
	unsigned int t, x;

	for (t = 0; t < POLL_COUNT; t++) {
		spin_lock_irqsave(&ensoniq->reg_lock, flags);
		if (!(inl(ES_REG(ensoniq, 1371_CODEC)) & ES_1371_CODEC_WIP)) {
			/* save the current state for latter */
			x = snd_es1371_wait_src_ready(ensoniq);
			outl((x & (ES_1371_SRC_DISABLE | ES_1371_DIS_P1 |
			           ES_1371_DIS_P2 | ES_1371_DIS_R1)) | 0x00010000,
			     ES_REG(ensoniq, 1371_SMPRATE));
			/* wait for not busy (state 0) first to avoid
			   transition states */
			for (t = 0; t < POLL_COUNT; t++) {
				if ((inl(ES_REG(ensoniq, 1371_SMPRATE)) & 0x00870000) == 0x00000000)
					break;
			}
			/* wait for a SAFE time to write addr/data and then do it, dammit */
			for (t = 0; t < POLL_COUNT; t++) {
				if ((inl(ES_REG(ensoniq, 1371_SMPRATE)) & 0x00870000) == 0x00010000)
					break;
			}
			outl(ES_1371_CODEC_WRITE(reg, val), ES_REG(ensoniq, 1371_CODEC));
			/* restore SRC reg */
			snd_es1371_wait_src_ready(ensoniq);
			outl(x, ES_REG(ensoniq, 1371_SMPRATE));
			spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
			return;
		}
		spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	}
	snd_printk("codec write timeout at 0x%lx [0x%x]\n", ES_REG(ensoniq, 1371_CODEC), inl(ES_REG(ensoniq, 1371_CODEC)));
}

static unsigned short snd_es1371_codec_read(ac97_t *ac97,
					    unsigned short reg)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, ac97->private_data, return -ENXIO);
	unsigned long flags;
	unsigned int t, x, fail = 0;

      __again:
	for (t = 0; t < POLL_COUNT; t++) {
		spin_lock_irqsave(&ensoniq->reg_lock, flags);
		if (!(inl(ES_REG(ensoniq, 1371_CODEC)) & ES_1371_CODEC_WIP)) {
			/* save the current state for latter */
			x = snd_es1371_wait_src_ready(ensoniq);
			outl((x & (ES_1371_SRC_DISABLE | ES_1371_DIS_P1 |
			           ES_1371_DIS_P2 | ES_1371_DIS_R1)) | 0x00010000,
			     ES_REG(ensoniq, 1371_SMPRATE));
			/* wait for not busy (state 0) first to avoid
			   transition states */
			for (t = 0; t < POLL_COUNT; t++) {
				if ((inl(ES_REG(ensoniq, 1371_SMPRATE)) & 0x00870000) == 0x00000000)
					break;
			}
			/* wait for a SAFE time to write addr/data and then do it, dammit */
			for (t = 0; t < POLL_COUNT; t++) {
				if ((inl(ES_REG(ensoniq, 1371_SMPRATE)) & 0x00870000) == 0x00010000)
					break;
			}
			outl(ES_1371_CODEC_READS(reg), ES_REG(ensoniq, 1371_CODEC));
			/* restore SRC reg */
			snd_es1371_wait_src_ready(ensoniq);
			outl(x, ES_REG(ensoniq, 1371_SMPRATE));
			/* wait for WIP again */
			for (t = 0; t < POLL_COUNT; t++) {
				if (!(inl(ES_REG(ensoniq, 1371_CODEC)) & ES_1371_CODEC_WIP))
					break;		
			}
			/* now wait for the stinkin' data (RDY) */
			for (t = 0; t < POLL_COUNT; t++) {
				if ((x = inl(ES_REG(ensoniq, 1371_CODEC))) & ES_1371_CODEC_RDY) {
					spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
					return ES_1371_CODEC_READ(x);
				}
			}
			spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
			if (++fail > 10) {
				snd_printk("codec read timeout (final) at 0x%lx, reg = 0x%x [0x%x]\n", ES_REG(ensoniq, 1371_CODEC), reg, inl(ES_REG(ensoniq, 1371_CODEC)));
				return 0;
			}
			goto __again;
		}
		spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	}
	snd_printk("es1371: codec read timeout at 0x%lx [0x%x]\n", ES_REG(ensoniq, 1371_CODEC), inl(ES_REG(ensoniq, 1371_CODEC)));
	return 0;
}

static void snd_es1371_adc_rate(ensoniq_t * ensoniq, unsigned int rate)
{
	unsigned int n, truncm, freq, result;

	n = rate / 3000;
	if ((1 << n) & ((1 << 15) | (1 << 13) | (1 << 11) | (1 << 9)))
		n--;
	truncm = (21 * n - 1) | 1;
	freq = ((48000UL << 15) / rate) * n;
	result = (48000UL << 15) / (freq / n);
	if (rate >= 24000) {
		if (truncm > 239)
			truncm = 239;
		snd_es1371_src_write(ensoniq, ES_SMPREG_ADC + ES_SMPREG_TRUNC_N,
				(((239 - truncm) >> 1) << 9) | (n << 4));
	} else {
		if (truncm > 119)
			truncm = 119;
		snd_es1371_src_write(ensoniq, ES_SMPREG_ADC + ES_SMPREG_TRUNC_N,
				0x8000 | (((119 - truncm) >> 1) << 9) | (n << 4));
	}
	snd_es1371_src_write(ensoniq, ES_SMPREG_ADC + ES_SMPREG_INT_REGS,
			     (snd_es1371_src_read(ensoniq, ES_SMPREG_ADC + ES_SMPREG_INT_REGS) & 0x00ff) |
				     ((freq >> 5) & 0xfc00));
	snd_es1371_src_write(ensoniq, ES_SMPREG_ADC + ES_SMPREG_VFREQ_FRAC, freq & 0x7fff);
	snd_es1371_src_write(ensoniq, ES_SMPREG_VOL_ADC, n << 8);
	snd_es1371_src_write(ensoniq, ES_SMPREG_VOL_ADC + 1, n << 8);
}

static void snd_es1371_dac1_rate(ensoniq_t * ensoniq, unsigned int rate)
{
	unsigned int freq, r;

	freq = (rate << 15) / 3000;
	r = (snd_es1371_wait_src_ready(ensoniq) & (ES_1371_SRC_DISABLE | ES_1371_DIS_P2 | ES_1371_DIS_R1)) | ES_1371_DIS_P1;
	outl(r, ES_REG(ensoniq, 1371_SMPRATE));
	snd_es1371_src_write(ensoniq, ES_SMPREG_DAC1 + ES_SMPREG_INT_REGS,
			     (snd_es1371_src_read(ensoniq, ES_SMPREG_DAC1 + ES_SMPREG_INT_REGS) & 0x00ff) |
			     ((freq >> 5) & 0xfc00));
	snd_es1371_src_write(ensoniq, ES_SMPREG_DAC1 + ES_SMPREG_VFREQ_FRAC, freq & 0x7fff);
	r = (snd_es1371_wait_src_ready(ensoniq) & (ES_1371_SRC_DISABLE | ES_1371_DIS_P2 | ES_1371_DIS_R1));
	outl(r, ES_REG(ensoniq, 1371_SMPRATE));
}

static void snd_es1371_dac2_rate(ensoniq_t * ensoniq, unsigned int rate)
{
	unsigned int freq, r;

	freq = (rate << 15) / 3000;
	r = (snd_es1371_wait_src_ready(ensoniq) & (ES_1371_SRC_DISABLE | ES_1371_DIS_P1 | ES_1371_DIS_R1)) | ES_1371_DIS_P2;
	outl(r, ES_REG(ensoniq, 1371_SMPRATE));
	snd_es1371_src_write(ensoniq, ES_SMPREG_DAC2 + ES_SMPREG_INT_REGS,
			     (snd_es1371_src_read(ensoniq, ES_SMPREG_DAC2 + ES_SMPREG_INT_REGS) & 0x00ff) |
			     ((freq >> 5) & 0xfc00));
	snd_es1371_src_write(ensoniq, ES_SMPREG_DAC2 + ES_SMPREG_VFREQ_FRAC, freq & 0x7fff);
	r = (snd_es1371_wait_src_ready(ensoniq) & (ES_1371_SRC_DISABLE | ES_1371_DIS_P1 | ES_1371_DIS_R1));
	outl(r, ES_REG(ensoniq, 1371_SMPRATE));
}

#endif /* CHIP1371 */

static int snd_ensoniq_trigger(snd_pcm_substream_t *substream, int cmd)
{
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	{
		unsigned int what = 0;
		snd_pcm_substream_t *s = substream;
		do {
			if (s == ensoniq->playback1_substream) {
				what |= ES_P1_PAUSE;
				snd_pcm_trigger_done(s, substream);
			} else if (s == ensoniq->playback2_substream) {
				what |= ES_P2_PAUSE;
				snd_pcm_trigger_done(s, substream);
			} else if (s == ensoniq->capture_substream)
				return -EINVAL;
			s = s->link_next;
		} while (s != substream);
		spin_lock(&ensoniq->reg_lock);
		if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH)
			ensoniq->sctrl |= what;
		else
			ensoniq->sctrl &= ~what;
		outl(ensoniq->sctrl, ES_REG(ensoniq, SERIAL));
		spin_unlock(&ensoniq->reg_lock);
		break;
	}
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
	{
		unsigned int what = 0;
		snd_pcm_substream_t *s = substream;
		do {
			if (s == ensoniq->playback1_substream) {
				what |= ES_DAC1_EN;
				snd_pcm_trigger_done(s, substream);
			} else if (s == ensoniq->playback2_substream) {
				what |= ES_DAC2_EN;
				snd_pcm_trigger_done(s, substream);
			} else if (s == ensoniq->capture_substream) {
				what |= ES_ADC_EN;
				snd_pcm_trigger_done(s, substream);
			}
			s = s->link_next;
		} while (s != substream);
		spin_lock(&ensoniq->reg_lock);
		if (cmd == SNDRV_PCM_TRIGGER_START)
			ensoniq->ctrl |= what;
		else
			ensoniq->ctrl &= ~what;
		outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
		spin_unlock(&ensoniq->reg_lock);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 *  PCM part
 */

static int snd_ensoniq_hw_params(snd_pcm_substream_t * substream,
				 snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_ensoniq_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_ensoniq_playback1_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int mode = 0;

	ensoniq->p1_dma_size = snd_pcm_lib_buffer_bytes(substream);
	ensoniq->p1_period_size = snd_pcm_lib_period_bytes(substream);
	if (snd_pcm_format_width(runtime->format) == 16)
		mode |= 0x02;
	if (runtime->channels > 1)
		mode |= 0x01;
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	ensoniq->ctrl &= ~ES_DAC1_EN;
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	outl(ES_MEM_PAGEO(ES_PAGE_DAC), ES_REG(ensoniq, MEM_PAGE));
	outl(runtime->dma_addr, ES_REG(ensoniq, DAC1_FRAME));
	outl((ensoniq->p1_dma_size >> 2) - 1, ES_REG(ensoniq, DAC1_SIZE));
	ensoniq->sctrl &= ~(ES_P1_LOOP_SEL | ES_P1_PAUSE | ES_P1_SCT_RLD | ES_P1_MODEM);
	ensoniq->sctrl |= ES_P1_INT_EN | ES_P1_MODEO(mode);
	outl(ensoniq->sctrl, ES_REG(ensoniq, SERIAL));
	outl((ensoniq->p1_period_size >> snd_ensoniq_sample_shift[mode]) - 1, ES_REG(ensoniq, DAC1_COUNT));
#ifdef CHIP1370
	ensoniq->ctrl &= ~ES_1370_WTSRSELM;
	switch (runtime->rate) {
	case 5512: ensoniq->ctrl |= ES_1370_WTSRSEL(0); break;
	case 11025: ensoniq->ctrl |= ES_1370_WTSRSEL(1); break;
	case 22050: ensoniq->ctrl |= ES_1370_WTSRSEL(2); break;
	case 44100: ensoniq->ctrl |= ES_1370_WTSRSEL(3); break;
	default: snd_BUG();
	}
#else
	snd_es1371_dac1_rate(ensoniq, runtime->rate);
#endif
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static int snd_ensoniq_playback2_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int mode = 0;

	ensoniq->p2_dma_size = snd_pcm_lib_buffer_bytes(substream);
	ensoniq->p2_period_size = snd_pcm_lib_period_bytes(substream);
	if (snd_pcm_format_width(runtime->format) == 16)
		mode |= 0x02;
	if (runtime->channels > 1)
		mode |= 0x01;
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	ensoniq->ctrl &= ~ES_DAC2_EN;
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	outl(ES_MEM_PAGEO(ES_PAGE_DAC), ES_REG(ensoniq, MEM_PAGE));
	outl(runtime->dma_addr, ES_REG(ensoniq, DAC2_FRAME));
	outl((ensoniq->p2_dma_size >> 2) - 1, ES_REG(ensoniq, DAC2_SIZE));
	ensoniq->sctrl &= ~(ES_P2_LOOP_SEL | ES_P2_PAUSE | ES_P2_DAC_SEN |
			    ES_P2_END_INCM | ES_P2_ST_INCM | ES_P2_MODEM);
	ensoniq->sctrl |= ES_P2_INT_EN | ES_P2_MODEO(mode) |
			  ES_P2_END_INCO(mode & 2 ? 2 : 1) | ES_P2_ST_INCO(0);
	outl(ensoniq->sctrl, ES_REG(ensoniq, SERIAL));
	outl((ensoniq->p2_period_size >> snd_ensoniq_sample_shift[mode]) - 1, ES_REG(ensoniq, DAC2_COUNT));
#ifdef CHIP1370
	if (!(ensoniq->u.es1370.pclkdiv_lock & ES_MODE_CAPTURE)) {
		ensoniq->ctrl &= ~ES_1370_PCLKDIVM;
		ensoniq->ctrl |= ES_1370_PCLKDIVO(ES_1370_SRTODIV(runtime->rate));
		ensoniq->u.es1370.pclkdiv_lock |= ES_MODE_PLAY2;
	}
#else
	snd_es1371_dac2_rate(ensoniq, runtime->rate);
#endif
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static int snd_ensoniq_capture_prepare(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int mode = 0;

	ensoniq->c_dma_size = snd_pcm_lib_buffer_bytes(substream);
	ensoniq->c_period_size = snd_pcm_lib_period_bytes(substream);
	if (snd_pcm_format_width(runtime->format) == 16)
		mode |= 0x02;
	if (runtime->channels > 1)
		mode |= 0x01;
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	ensoniq->ctrl &= ~ES_ADC_EN;
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	outl(ES_MEM_PAGEO(ES_PAGE_ADC), ES_REG(ensoniq, MEM_PAGE));
	outl(runtime->dma_addr, ES_REG(ensoniq, ADC_FRAME));
	outl((ensoniq->c_dma_size >> 2) - 1, ES_REG(ensoniq, ADC_SIZE));
	ensoniq->sctrl &= ~(ES_R1_LOOP_SEL | ES_R1_MODEM);
	ensoniq->sctrl |= ES_R1_INT_EN | ES_R1_MODEO(mode);
	outl(ensoniq->sctrl, ES_REG(ensoniq, SERIAL));
	outl((ensoniq->c_period_size >> snd_ensoniq_sample_shift[mode]) - 1, ES_REG(ensoniq, ADC_COUNT));
#ifdef CHIP1370
	if (!(ensoniq->u.es1370.pclkdiv_lock & ES_MODE_PLAY2)) {
		ensoniq->ctrl &= ~ES_1370_PCLKDIVM;
		ensoniq->ctrl |= ES_1370_PCLKDIVO(ES_1370_SRTODIV(runtime->rate));
		ensoniq->u.es1370.pclkdiv_lock |= ES_MODE_CAPTURE;
	}
#else
	snd_es1371_adc_rate(ensoniq, runtime->rate);
#endif
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static snd_pcm_uframes_t snd_ensoniq_playback1_pointer(snd_pcm_substream_t * substream)
{
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	size_t ptr;

	spin_lock(&ensoniq->reg_lock);
	if (inl(ES_REG(ensoniq, CONTROL)) & ES_DAC1_EN) {
		outl(ES_MEM_PAGEO(ES_PAGE_DAC), ES_REG(ensoniq, MEM_PAGE));
		ptr = ES_REG_FCURR_COUNTI(inl(ES_REG(ensoniq, DAC1_SIZE)));
		ptr = bytes_to_frames(substream->runtime, ptr);
	} else {
		ptr = 0;
	}
	spin_unlock(&ensoniq->reg_lock);
	return ptr;
}

static snd_pcm_uframes_t snd_ensoniq_playback2_pointer(snd_pcm_substream_t * substream)
{
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	size_t ptr;

	spin_lock(&ensoniq->reg_lock);
	if (inl(ES_REG(ensoniq, CONTROL)) & ES_DAC2_EN) {
		outl(ES_MEM_PAGEO(ES_PAGE_DAC), ES_REG(ensoniq, MEM_PAGE));
		ptr = ES_REG_FCURR_COUNTI(inl(ES_REG(ensoniq, DAC2_SIZE)));
		ptr = bytes_to_frames(substream->runtime, ptr);
	} else {
		ptr = 0;
	}
	spin_unlock(&ensoniq->reg_lock);
	return ptr;
}

static snd_pcm_uframes_t snd_ensoniq_capture_pointer(snd_pcm_substream_t * substream)
{
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	size_t ptr;

	spin_lock(&ensoniq->reg_lock);
	if (inl(ES_REG(ensoniq, CONTROL)) & ES_ADC_EN) {
		outl(ES_MEM_PAGEO(ES_PAGE_ADC), ES_REG(ensoniq, MEM_PAGE));
		ptr = ES_REG_FCURR_COUNTI(inl(ES_REG(ensoniq, ADC_SIZE)));
		ptr = bytes_to_frames(substream->runtime, ptr);
	} else {
		ptr = 0;
	}
	spin_unlock(&ensoniq->reg_lock);
	return ptr;
}

#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_ensoniq_playback1 =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:            */  
#ifndef CHIP1370
				SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
#else
				(SNDRV_PCM_RATE_KNOT | 	/* 5512Hz rate */
				 SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_22050 | 
				 SNDRV_PCM_RATE_44100),
#endif
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max: */	(128*1024),
/*	period_bytes_min: */	64,
/*	period_bytes_max: */	(128*1024),
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

static snd_pcm_hardware_t snd_ensoniq_playback2 =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_PAUSE | 
				 SNDRV_PCM_INFO_SYNC_START),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max: */	(128*1024),
/*	period_bytes_min: */ 	64,
/*	period_bytes_max: */	(128*1024),
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};

static snd_pcm_hardware_t snd_ensoniq_capture =
{
/*	info:		  */	(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
/*	formats:	  */	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
/*	rates:		  */	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:	  */	4000,
/*	rate_max:	  */	48000,
/*	channels_min:	  */	1,
/*	channels_max:	  */	2,
/*	buffer_bytes_max: */	(128*1024),
/*	period_bytes_min: */	64,
/*	period_bytes_max: */	(128*1024),
/*	periods_min:	  */	1,
/*	periods_max:	  */	1024,
/*	fifo_size:	  */	0,
};
#else
static snd_pcm_hardware_t snd_ensoniq_playback1 =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_SYNC_START),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:
#ifndef CHIP1370
				SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
#else
				(SNDRV_PCM_RATE_KNOT | 	/* 5512Hz rate */
				 SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_22050 | 
				 SNDRV_PCM_RATE_44100),
#endif
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(128*1024),
	period_bytes_min:	64,
	period_bytes_max:	(128*1024),
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_ensoniq_playback2 =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_PAUSE | 
				 SNDRV_PCM_INFO_SYNC_START),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(128*1024),
	period_bytes_min:	64,
	period_bytes_max:	(128*1024),
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_ensoniq_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(128*1024),
	period_bytes_min:	64,
	period_bytes_max:	(128*1024),
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};
#endif

static int snd_ensoniq_playback1_open(snd_pcm_substream_t * substream)
{
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	ensoniq->mode |= ES_MODE_PLAY1;
	ensoniq->playback1_substream = substream;
	runtime->hw = snd_ensoniq_playback1;
	snd_pcm_set_sync(substream);
#ifdef CHIP1370
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &snd_es1370_hw_constraints_rates);
#else
	snd_pcm_hw_constraint_ratdens(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &snd_es1371_hw_constraints_dac_clock);
#endif
	return 0;
}

static int snd_ensoniq_playback2_open(snd_pcm_substream_t * substream)
{
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	ensoniq->mode |= ES_MODE_PLAY2;
	ensoniq->playback2_substream = substream;
	runtime->hw = snd_ensoniq_playback2;
	snd_pcm_set_sync(substream);
#ifdef CHIP1370
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &snd_es1370_hw_constraints_clock);
#else
	snd_pcm_hw_constraint_ratdens(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &snd_es1371_hw_constraints_dac_clock);
#endif
	return 0;
}

static int snd_ensoniq_capture_open(snd_pcm_substream_t * substream)
{
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	ensoniq->mode |= ES_MODE_CAPTURE;
	ensoniq->capture_substream = substream;
	runtime->hw = snd_ensoniq_capture;
	snd_pcm_set_sync(substream);
#ifdef CHIP1370
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &snd_es1370_hw_constraints_clock);
#else
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &snd_es1371_hw_constraints_adc_clock);
#endif
	return 0;
}

static int snd_ensoniq_playback1_close(snd_pcm_substream_t * substream)
{
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);

	ensoniq->playback1_substream = NULL;
	ensoniq->mode &= ~ES_MODE_PLAY1;
	return 0;
}

static int snd_ensoniq_playback2_close(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);

	ensoniq->playback2_substream = NULL;
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
#ifdef CHIP1370
	ensoniq->u.es1370.pclkdiv_lock &= ~ES_MODE_PLAY2;
#endif
	ensoniq->mode &= ~ES_MODE_PLAY2;
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static int snd_ensoniq_capture_close(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_pcm_substream_chip(substream);

	ensoniq->capture_substream = NULL;
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
#ifdef CHIP1370
	ensoniq->u.es1370.pclkdiv_lock &= ~ES_MODE_CAPTURE;
#endif
	ensoniq->mode &= ~ES_MODE_CAPTURE;
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

#ifdef TARGET_OS2
static snd_pcm_ops_t snd_ensoniq_playback1_ops = {
/*	open:	  */	snd_ensoniq_playback1_open,
/*	close:	  */	snd_ensoniq_playback1_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_ensoniq_hw_params,
/*	hw_free:  */	snd_ensoniq_hw_free,
/*	prepare:  */	snd_ensoniq_playback1_prepare,
/*	trigger:  */	snd_ensoniq_trigger,
/*	pointer:  */	snd_ensoniq_playback1_pointer,
 	0, 0
};

static snd_pcm_ops_t snd_ensoniq_playback2_ops = {
/*	open:	  */	snd_ensoniq_playback2_open,
/*	close:	  */	snd_ensoniq_playback2_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_ensoniq_hw_params,
/*	hw_free:  */	snd_ensoniq_hw_free,
/*	prepare:  */	snd_ensoniq_playback2_prepare,
/*	trigger:  */	snd_ensoniq_trigger,
/*	pointer:  */	snd_ensoniq_playback2_pointer,
 	0, 0
};

static snd_pcm_ops_t snd_ensoniq_capture_ops = {
/*	open:	  */	snd_ensoniq_capture_open,
/*	close:	  */	snd_ensoniq_capture_close,
/*	ioctl:	  */	snd_pcm_lib_ioctl,
/*	hw_params:*/	snd_ensoniq_hw_params,
/*	hw_free:  */	snd_ensoniq_hw_free,
/*	prepare:  */	snd_ensoniq_capture_prepare,
/*	trigger:  */	snd_ensoniq_trigger,
/*	pointer:  */	snd_ensoniq_capture_pointer,
 	0, 0
};
#else
static snd_pcm_ops_t snd_ensoniq_playback1_ops = {
	open:		snd_ensoniq_playback1_open,
	close:		snd_ensoniq_playback1_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_ensoniq_hw_params,
	hw_free:	snd_ensoniq_hw_free,
	prepare:	snd_ensoniq_playback1_prepare,
	trigger:	snd_ensoniq_trigger,
	pointer:	snd_ensoniq_playback1_pointer,
};

static snd_pcm_ops_t snd_ensoniq_playback2_ops = {
	open:		snd_ensoniq_playback2_open,
	close:		snd_ensoniq_playback2_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_ensoniq_hw_params,
	hw_free:	snd_ensoniq_hw_free,
	prepare:	snd_ensoniq_playback2_prepare,
	trigger:	snd_ensoniq_trigger,
	pointer:	snd_ensoniq_playback2_pointer,
};

static snd_pcm_ops_t snd_ensoniq_capture_ops = {
	open:		snd_ensoniq_capture_open,
	close:		snd_ensoniq_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_ensoniq_hw_params,
	hw_free:	snd_ensoniq_hw_free,
	prepare:	snd_ensoniq_capture_prepare,
	trigger:	snd_ensoniq_trigger,
	pointer:	snd_ensoniq_capture_pointer,
};
#endif

static void snd_ensoniq_pcm_free(snd_pcm_t *pcm)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, pcm->private_data, return);
	ensoniq->pcm1 = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __init snd_ensoniq_pcm(ensoniq_t * ensoniq, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
#ifdef CHIP1370
	err = snd_pcm_new(ensoniq->card, "ES1370/1", device, 1, 1, &pcm);
#else
	err = snd_pcm_new(ensoniq->card, "ES1371/1", device, 1, 1, &pcm);
#endif
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ensoniq_playback2_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ensoniq_capture_ops);

	pcm->private_data = ensoniq;
	pcm->private_free = snd_ensoniq_pcm_free;
	pcm->info_flags = 0;
#ifdef CHIP1370
	strcpy(pcm->name, "ES1370 DAC2/ADC");
#else
	strcpy(pcm->name, "ES1371 DAC2/ADC");
#endif
	ensoniq->pcm1 = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(ensoniq->pci, pcm, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

static void snd_ensoniq_pcm_free2(snd_pcm_t *pcm)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, pcm->private_data, return);
	ensoniq->pcm2 = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __init snd_ensoniq_pcm2(ensoniq_t * ensoniq, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
#ifdef CHIP1370
	err = snd_pcm_new(ensoniq->card, "ES1370/2", device, 1, 0, &pcm);
#else
	err = snd_pcm_new(ensoniq->card, "ES1371/2", device, 1, 0, &pcm);
#endif
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ensoniq_playback1_ops);

	pcm->private_data = ensoniq;
	pcm->private_free = snd_ensoniq_pcm_free2;
	pcm->info_flags = 0;
#ifdef CHIP1370
	strcpy(pcm->name, "ES1370 DAC1");
#else
	strcpy(pcm->name, "ES1371 DAC1");
#endif
	ensoniq->pcm2 = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(ensoniq->pci, pcm, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  Mixer section
 */

#ifdef CHIP1371

#ifdef TARGET_OS2
#define ES1371_SPDIF(xname) \
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, xname, 0, 0, snd_es1371_spdif_info, \
  snd_es1371_spdif_get, snd_es1371_spdif_put, 0 }
#else
#define ES1371_SPDIF(xname) \
{ iface: SNDRV_CTL_ELEM_IFACE_MIXER, name: xname, info: snd_es1371_spdif_info, \
  get: snd_es1371_spdif_get, put: snd_es1371_spdif_put }
#endif

static int snd_es1371_spdif_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
        uinfo->count = 1;
        uinfo->value.integer.min = 0;
        uinfo->value.integer.max = 1;
        return 0;
}

static int snd_es1371_spdif_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ensoniq_t *ensoniq = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	ucontrol->value.integer.value[0] = ensoniq->ctrl & ES_1371_SPDIF_EN ? 1 : 0;
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static int snd_es1371_spdif_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ensoniq_t *ensoniq = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int nval1, nval2;
	int change;
	
	nval1 = ucontrol->value.integer.value[0] ? ES_1371_SPDIF_EN : 0;
	nval2 = ucontrol->value.integer.value[0] ? ES_1371_ST_SPDIF_EN : 0;
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	change = (ensoniq->ctrl & ES_1371_SPDIF_EN) != nval1;
	ensoniq->ctrl &= ~ES_1371_SPDIF_EN;
	ensoniq->ctrl |= nval1;
	ensoniq->cssr &= ~ES_1371_ST_SPDIF_EN;
	ensoniq->cssr |= nval2;
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	outl(ensoniq->cssr, ES_REG(ensoniq, STATUS));
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_es1371_mixer_spdif =
ES1371_SPDIF("IEC958 Playback Switch");

static void snd_ensoniq_mixer_free_ac97(ac97_t *ac97)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, ac97->private_data, return);
	ensoniq->u.es1371.ac97 = NULL;
}

static int snd_ensoniq_1371_mixer(ensoniq_t * ensoniq)
{
	snd_card_t *card = ensoniq->card;
	ac97_t ac97;
	int err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.write = snd_es1371_codec_write;
	ac97.read = snd_es1371_codec_read;
	ac97.private_data = ensoniq;
	ac97.private_free = snd_ensoniq_mixer_free_ac97;
	if ((err = snd_ac97_mixer(card, &ac97, &ensoniq->u.es1371.ac97)) < 0)
		return err;
	if (ensoniq->rev >= ES1371REV_ES1373_A)
		snd_ctl_add(card, snd_ctl_new1(&snd_es1371_mixer_spdif, ensoniq));
	return 0;
}

#endif /* CHIP1371 */

#ifdef TARGET_OS2
#define ENSONIQ_CONTROL(xname, mask) \
{ SNDRV_CTL_ELEM_IFACE_CARD, 0, 0, xname, 0, 0, snd_ensoniq_control_info,\
  snd_ensoniq_control_get, snd_ensoniq_control_put, \
  mask }
#else
#define ENSONIQ_CONTROL(xname, mask) \
{ iface: SNDRV_CTL_ELEM_IFACE_CARD, name: xname, info: snd_ensoniq_control_info, \
  get: snd_ensoniq_control_get, put: snd_ensoniq_control_put, \
  private_value: mask }
#endif

static int snd_ensoniq_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
        uinfo->count = 1;
        uinfo->value.integer.min = 0;
        uinfo->value.integer.max = 1;
        return 0;
}

static int snd_ensoniq_control_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ensoniq_t *ensoniq = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int mask = kcontrol->private_value;
	
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	ucontrol->value.integer.value[0] = ensoniq->ctrl & mask ? 1 : 0;
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static int snd_ensoniq_control_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ensoniq_t *ensoniq = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int mask = kcontrol->private_value;
	unsigned int nval;
	int change;
	
	nval = ucontrol->value.integer.value[0] ? mask : 0;
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	change = (ensoniq->ctrl & mask) != nval;
	ensoniq->ctrl &= ~mask;
	ensoniq->ctrl |= nval;
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return change;
}

#ifdef CHIP1370

#define ES1370_CONTROLS 2

static snd_kcontrol_new_t snd_es1370_controls[2] = {
ENSONIQ_CONTROL("PCM 0 Output also on Line-In Jack", ES_1370_XCTL0),
ENSONIQ_CONTROL("Mic +5V bias", ES_1370_XCTL1)
};

static void snd_ensoniq_mixer_free_ak4531(ak4531_t *ak4531)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, ak4531->private_data, return);
	ensoniq->u.es1370.ak4531 = NULL;
}

static int __init snd_ensoniq_1370_mixer(ensoniq_t * ensoniq)
{
	snd_card_t *card = ensoniq->card;
	ak4531_t ak4531;
	int err, idx;

	/* try reset AK4531 */
	outw(ES_1370_CODEC_WRITE(AK4531_RESET, 0x02), ES_REG(ensoniq, 1370_CODEC));
	udelay(100);
	outw(ES_1370_CODEC_WRITE(AK4531_RESET, 0x03), ES_REG(ensoniq, 1370_CODEC));
	udelay(100);

	memset(&ak4531, 0, sizeof(ak4531));
	ak4531.write = snd_es1370_codec_write;
	ak4531.private_data = ensoniq;
	ak4531.private_free = snd_ensoniq_mixer_free_ak4531;
	if ((err = snd_ak4531_mixer(card, &ak4531, &ensoniq->u.es1370.ak4531)) < 0)
		return err;
	for (idx = 0; idx < ES1370_CONTROLS; idx++)
		snd_ctl_add(card, snd_ctl_new1(&snd_es1370_controls[idx], ensoniq));
	return 0;
}

#endif /* CHIP1370 */

/*
 *  General Switches...
 */

static snd_kcontrol_new_t snd_ensoniq_control_joystick =
ENSONIQ_CONTROL("Joystick Enable", ES_JYSTK_EN);

#ifdef CHIP1371

#ifdef TARGET_OS2
#define ES1371_JOYSTICK_ADDR(xname) \
{ SNDRV_CTL_ELEM_IFACE_CARD, 0, 0, xname, 0, 0, snd_es1371_joystick_addr_info, \
  snd_es1371_joystick_addr_get, snd_es1371_joystick_addr_put, 0 }
#else
#define ES1371_JOYSTICK_ADDR(xname) \
{ iface: SNDRV_CTL_ELEM_IFACE_CARD, name: xname, info: snd_es1371_joystick_addr_info, \
  get: snd_es1371_joystick_addr_get, put: snd_es1371_joystick_addr_put }
#endif

static int snd_es1371_joystick_addr_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
        uinfo->count = 1;
        uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 3)
		uinfo->value.enumerated.item = 3;
	sprintf(uinfo->value.enumerated.name, "port 0x%x", (uinfo->value.enumerated.item * 8) + 0x200);
        return 0;
}

static int snd_es1371_joystick_addr_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ensoniq_t *ensoniq = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	ucontrol->value.enumerated.item[0] = ES_1371_JOY_ASELI(ensoniq->ctrl);
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static int snd_es1371_joystick_addr_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ensoniq_t *ensoniq = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int nval;
	int change;
	
	nval = ES_1371_JOY_ASEL(ucontrol->value.integer.value[0]);
	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	change = (ensoniq->ctrl & ES_1371_JOY_ASELM) != nval;
	ensoniq->ctrl &= ~ES_1371_JOY_ASELM;
	ensoniq->ctrl |= nval;
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_es1371_joystick_addr =
ES1371_JOYSTICK_ADDR("Joystick Address");

#endif /* CHIP1371 */

/*

 */

static void snd_ensoniq_proc_read(snd_info_entry_t *entry, 
				  snd_info_buffer_t * buffer)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, entry->private_data, return);

#ifdef CHIP1370
	snd_iprintf(buffer, "Ensoniq AudioPCI ES1370\n\n");
#else
	snd_iprintf(buffer, "Ensoniq AudioPCI ES1371\n\n");
#endif
	snd_iprintf(buffer, "Joystick enable  : %s\n", ensoniq->ctrl & ES_JYSTK_EN ? "on" : "off");
#ifdef CHIP1370
	snd_iprintf(buffer, "MIC +5V bias     : %s\n", ensoniq->ctrl & ES_1370_XCTL1 ? "on" : "off");
	snd_iprintf(buffer, "Line In to AOUT  : %s\n", ensoniq->ctrl & ES_1370_XCTL0 ? "on" : "off");
#else
	snd_iprintf(buffer, "Joystick port    : 0x%x\n", (ES_1371_JOY_ASELI(ensoniq->ctrl) * 8) + 0x200);
#endif
}

static void __init snd_ensoniq_proc_init(ensoniq_t * ensoniq)
{
	snd_info_entry_t *entry;

	if ((entry = snd_info_create_card_entry(ensoniq->card, "audiopci", ensoniq->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = ensoniq;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_ensoniq_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ensoniq->proc_entry = entry;
}

static void snd_ensoniq_proc_done(ensoniq_t * ensoniq)
{
	if (ensoniq->proc_entry) {
		snd_info_unregister(ensoniq->proc_entry);
		ensoniq->proc_entry = NULL;
	}
}

/*

 */

static int snd_ensoniq_free(ensoniq_t *ensoniq)
{
	snd_ensoniq_proc_done(ensoniq);
	if (ensoniq->irq < 0)
		goto __hw_end;
#ifdef CHIP1370
	outl(ES_1370_SERR_DISABLE, ES_REG(ensoniq, CONTROL));	/* switch everything off */
	outl(0, ES_REG(ensoniq, SERIAL));	/* clear serial interface */
#else
	outl(0, ES_REG(ensoniq, CONTROL));	/* switch everything off */
	outl(0, ES_REG(ensoniq, SERIAL));	/* clear serial interface */
#endif
	synchronize_irq();
	pci_set_power_state(ensoniq->pci, 3);
      __hw_end:
#ifdef CHIP1370
	if (ensoniq->bugbuf)
		snd_free_pci_pages(ensoniq->pci, 16, ensoniq->bugbuf, ensoniq->bugbuf_addr);
#endif
	if (ensoniq->res_port)
		release_resource(ensoniq->res_port);
	if (ensoniq->irq >= 0)
		free_irq(ensoniq->irq, (void *)ensoniq);
	snd_magic_kfree(ensoniq);
	return 0;
}

static int snd_ensoniq_dev_free(snd_device_t *device)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, device->device_data, return -ENXIO);
	return snd_ensoniq_free(ensoniq);
}

#ifdef CHIP1371
static struct {
	unsigned short svid;		/* subsystem vendor ID */
	unsigned short sdid;		/* subsystem device ID */
} es1371_amplifier_hack[] = {
#ifdef TARGET_OS2
	{ 0x107b, 0x2150 },	/* Gateway Solo 2150 */
	{ 0x13bd, 0x100c },	/* EV1938 on Mebius PC-MJ100V */
	{ 0x1102, 0x5938 },	/* Targa Xtender300 */
	{ 0x1102, 0x8938 }, /* IPC Topnote G notebook */
	{ PCI_ANY_ID, PCI_ANY_ID }
#else
	{ svid: 0x107b, sdid: 0x2150 },	/* Gateway Solo 2150 */
	{ svid: 0x13bd, sdid: 0x100c },	/* EV1938 on Mebius PC-MJ100V */
	{ svid: 0x1102, sdid: 0x5938 },	/* Targa Xtender300 */
	{ svid: 0x1102, sdid: 0x8938 }, /* IPC Topnote G notebook */
	{ svid: PCI_ANY_ID, sdid: PCI_ANY_ID }
#endif
};
static struct {
	unsigned short vid;		/* vendor ID */
	unsigned short did;		/* device ID */
	unsigned char rev;		/* revision */
} es1371_ac97_reset_hack[] = {
#ifdef TARGET_OS2
	{ PCI_VENDOR_ID_ENSONIQ, PCI_DEVICE_ID_ENSONIQ_CT5880, CT5880REV_CT5880_C },
	{ PCI_VENDOR_ID_ENSONIQ, PCI_DEVICE_ID_ENSONIQ_CT5880, CT5880REV_CT5880_D },
	{ PCI_VENDOR_ID_ENSONIQ, PCI_DEVICE_ID_ENSONIQ_ES1371, ES1371REV_CT5880_A },
	{ PCI_VENDOR_ID_ENSONIQ, PCI_DEVICE_ID_ENSONIQ_ES1371, ES1371REV_ES1373_8 },
	{ PCI_ANY_ID, PCI_ANY_ID, 0 }
#else
	{ vid: PCI_VENDOR_ID_ENSONIQ, did: PCI_DEVICE_ID_ENSONIQ_CT5880, rev: CT5880REV_CT5880_C },
	{ vid: PCI_VENDOR_ID_ENSONIQ, did: PCI_DEVICE_ID_ENSONIQ_CT5880, rev: CT5880REV_CT5880_D },
	{ vid: PCI_VENDOR_ID_ENSONIQ, did: PCI_DEVICE_ID_ENSONIQ_ES1371, rev: ES1371REV_CT5880_A },
	{ vid: PCI_VENDOR_ID_ENSONIQ, did: PCI_DEVICE_ID_ENSONIQ_ES1371, rev: ES1371REV_ES1373_8 },
	{ vid: PCI_ANY_ID, did: PCI_ANY_ID, rev: 0 }
#endif
};
#endif

static int __init snd_ensoniq_create(snd_card_t * card,
				     struct pci_dev *pci,
				     ensoniq_t ** rensoniq)
{
	ensoniq_t *ensoniq;
	unsigned short cmdw;
	unsigned char cmdb;
#ifdef CHIP1371
	int idx;
#endif
	int err;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_ensoniq_dev_free,0,0
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_ensoniq_dev_free,
	};
#endif

	*rensoniq = NULL;
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	ensoniq = snd_magic_kcalloc(ensoniq_t, 0, GFP_KERNEL);
	if (ensoniq == NULL)
		return -ENOMEM;
	spin_lock_init(&ensoniq->reg_lock);
	ensoniq->card = card;
	ensoniq->pci = pci;
	ensoniq->irq = -1;
	ensoniq->port = pci_resource_start(pci, 0);
	if ((ensoniq->res_port = request_region(ensoniq->port, 0x40, "Ensoniq AudioPCI")) == NULL) {
		snd_ensoniq_free(ensoniq);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", ensoniq->port, ensoniq->port + 0x40 - 1);
		return -EBUSY;
	}
	if (request_irq(pci->irq, snd_audiopci_interrupt, SA_INTERRUPT|SA_SHIRQ, "Ensoniq AudioPCI", (void *)ensoniq)) {
		snd_ensoniq_free(ensoniq);
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	ensoniq->irq = pci->irq;
#ifdef CHIP1370
	if ((ensoniq->bugbuf = snd_malloc_pci_pages(pci, 16, &ensoniq->bugbuf_addr)) == NULL) {
		snd_ensoniq_free(ensoniq);
		snd_printk("unable to allocate space for phantom area - bugbuf\n");
		return -EBUSY;
	}
#endif
	pci_set_master(pci);
	pci_read_config_byte(pci, PCI_REVISION_ID, &cmdb);
	ensoniq->rev = cmdb;
	pci_read_config_word(pci, PCI_SUBSYSTEM_VENDOR_ID, &cmdw);
	ensoniq->subsystem_vendor_id = cmdw;
	pci_read_config_word(pci, PCI_SUBSYSTEM_ID, &cmdw);
	ensoniq->subsystem_device_id = cmdw;
	snd_ensoniq_proc_init(ensoniq);
#ifdef CHIP1370
#if 0
	ensoniq->ctrl = ES_1370_CDC_EN | ES_1370_SERR_DISABLE | ES_1370_PCLKDIVO(ES_1370_SRTODIV(8000));
#else	/* get microphone working */
	ensoniq->ctrl = ES_1370_CDC_EN | ES_1370_PCLKDIVO(ES_1370_SRTODIV(8000));
#endif
	ensoniq->sctrl = 0;
	/* initialize the chips */
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	outl(ensoniq->sctrl, ES_REG(ensoniq, SERIAL));
	outl(ES_MEM_PAGEO(ES_PAGE_ADC), ES_REG(ensoniq, MEM_PAGE));
	outl(ensoniq->bugbuf_addr, ES_REG(ensoniq, PHANTOM_FRAME));
	outl(0, ES_REG(ensoniq, PHANTOM_COUNT));
#else
	ensoniq->ctrl = 0;
	ensoniq->sctrl = 0;
	ensoniq->cssr = 0;
	for (idx = 0; es1371_amplifier_hack[idx].svid != (unsigned short)PCI_ANY_ID; idx++)
		if (ensoniq->subsystem_vendor_id == es1371_amplifier_hack[idx].svid &&
		    ensoniq->subsystem_device_id == es1371_amplifier_hack[idx].sdid) {
			ensoniq->ctrl |= ES_1371_GPIO_OUT(1);	/* turn amplifier on */
			break;
		}
	/* initialize the chips */
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	outl(ensoniq->sctrl, ES_REG(ensoniq, SERIAL));
	outl(0, ES_REG(ensoniq, 1371_LEGACY));
	for (idx = 0; es1371_ac97_reset_hack[idx].vid != (unsigned short)PCI_ANY_ID; idx++)
		if (pci->vendor == es1371_ac97_reset_hack[idx].vid &&
		    pci->device == es1371_ac97_reset_hack[idx].did &&
		    ensoniq->rev == es1371_ac97_reset_hack[idx].rev) {
		        unsigned long tmo;
			signed long tmo2;

			ensoniq->cssr |= ES_1371_ST_AC97_RST;
			outl(ensoniq->cssr, ES_REG(ensoniq, STATUS));
			/* need to delay around 20ms(bleech) to give
			some CODECs enough time to wakeup */
			tmo = jiffies + (HZ / 50) + 1;
			while (1) {
				tmo2 = tmo - jiffies;
				if (tmo2 <= 0)
					break;
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(tmo2);
			}
			break;
		}
	/* AC'97 warm reset to start the bitclk */
	outl(ensoniq->ctrl | ES_1371_SYNC_RES, ES_REG(ensoniq, CONTROL));
	udelay(20);
	outl(ensoniq->ctrl, ES_REG(ensoniq, CONTROL));
	/* Init the sample rate converter */
	snd_es1371_wait_src_ready(ensoniq);	
	outl(ES_1371_SRC_DISABLE, ES_REG(ensoniq, 1371_SMPRATE));
	for (idx = 0; idx < 0x80; idx++)
		snd_es1371_src_write(ensoniq, idx, 0);
	snd_es1371_src_write(ensoniq, ES_SMPREG_DAC1 + ES_SMPREG_TRUNC_N, 16 << 4);
	snd_es1371_src_write(ensoniq, ES_SMPREG_DAC1 + ES_SMPREG_INT_REGS, 16 << 10);
	snd_es1371_src_write(ensoniq, ES_SMPREG_DAC2 + ES_SMPREG_TRUNC_N, 16 << 4);
	snd_es1371_src_write(ensoniq, ES_SMPREG_DAC2 + ES_SMPREG_INT_REGS, 16 << 10);
	snd_es1371_src_write(ensoniq, ES_SMPREG_VOL_ADC, 1 << 12);
	snd_es1371_src_write(ensoniq, ES_SMPREG_VOL_ADC + 1, 1 << 12);
	snd_es1371_src_write(ensoniq, ES_SMPREG_VOL_DAC1, 1 << 12);
	snd_es1371_src_write(ensoniq, ES_SMPREG_VOL_DAC1 + 1, 1 << 12);
	snd_es1371_src_write(ensoniq, ES_SMPREG_VOL_DAC2, 1 << 12);
	snd_es1371_src_write(ensoniq, ES_SMPREG_VOL_DAC2 + 1, 1 << 12);
	snd_es1371_adc_rate(ensoniq, 22050);
	snd_es1371_dac1_rate(ensoniq, 22050);
	snd_es1371_dac2_rate(ensoniq, 22050);
	/* WARNING:
	 * enabling the sample rate converter without properly programming
	 * its parameters causes the chip to lock up (the SRC busy bit will
	 * be stuck high, and I've found no way to rectify this other than
	 * power cycle) - Thomas Sailer
	 */
	snd_es1371_wait_src_ready(ensoniq);
	outl(0, ES_REG(ensoniq, 1371_SMPRATE));
	/* try reset codec directly */
	outl(ES_1371_CODEC_WRITE(0, 0), ES_REG(ensoniq, 1371_CODEC));
#endif
	outb(ensoniq->uartc = 0x00, ES_REG(ensoniq, UART_CONTROL));
	outb(0x00, ES_REG(ensoniq, UART_RES));
	outl(ensoniq->cssr, ES_REG(ensoniq, STATUS));
	snd_ctl_add(card, snd_ctl_new1(&snd_ensoniq_control_joystick, ensoniq));
#ifdef CHIP1371
	snd_ctl_add(card, snd_ctl_new1(&snd_es1371_joystick_addr, ensoniq));
#endif
	synchronize_irq();

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, ensoniq, &ops)) < 0) {
		snd_ensoniq_free(ensoniq);
		return err;
	}

	*rensoniq = ensoniq;
	return 0;
}

/*
 *  MIDI section
 */

static void snd_ensoniq_midi_interrupt(ensoniq_t * ensoniq)
{
	snd_rawmidi_t * rmidi = ensoniq->rmidi;
	unsigned char status, mask, byte;

	if (rmidi == NULL)
		return;
	/* do Rx at first */
	spin_lock(&ensoniq->reg_lock);
	mask = ensoniq->uartm & ES_MODE_INPUT ? ES_RXRDY : 0;
	while (mask) {
		status = inb(ES_REG(ensoniq, UART_STATUS));
		if ((status & mask) == 0)
			break;
		byte = inb(ES_REG(ensoniq, UART_DATA));
		spin_unlock(&ensoniq->reg_lock);
		snd_rawmidi_receive(ensoniq->midi_input, &byte, 1);
		spin_lock(&ensoniq->reg_lock);
	}
	spin_unlock(&ensoniq->reg_lock);

	/* do Tx at second */
	spin_lock(&ensoniq->reg_lock);
	mask = ensoniq->uartm & ES_MODE_OUTPUT ? ES_TXRDY : 0;
	while (mask) {
		status = inb(ES_REG(ensoniq, UART_STATUS));
		if ((status & mask) == 0)
			break;
		if (snd_rawmidi_transmit(ensoniq->midi_output, &byte, 1) != 1) {
			ensoniq->uartc &= ~ES_TXINTENM;
			outb(ensoniq->uartc, ES_REG(ensoniq, UART_CONTROL));
			mask &= ~ES_TXRDY;
		} else {
			outb(byte, ES_REG(ensoniq, UART_DATA));
		}
	}
	spin_unlock(&ensoniq->reg_lock);
}

static int snd_ensoniq_midi_input_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	ensoniq->uartm |= ES_MODE_INPUT;
	ensoniq->midi_input = substream;
	if (!(ensoniq->uartm & ES_MODE_OUTPUT)) {
		outb(ES_CNTRL(3), ES_REG(ensoniq, UART_CONTROL));
		outb(ensoniq->uartc = 0, ES_REG(ensoniq, UART_CONTROL));
		outl(ensoniq->ctrl |= ES_UART_EN, ES_REG(ensoniq, CONTROL));
	}
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static int snd_ensoniq_midi_input_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	if (!(ensoniq->uartm & ES_MODE_OUTPUT)) {
		outb(ensoniq->uartc = 0, ES_REG(ensoniq, UART_CONTROL));
		outl(ensoniq->ctrl &= ~ES_UART_EN, ES_REG(ensoniq, CONTROL));
	} else {
		outb(ensoniq->uartc &= ~ES_RXINTEN, ES_REG(ensoniq, UART_CONTROL));
	}
	ensoniq->midi_input = NULL;
	ensoniq->uartm &= ~ES_MODE_INPUT;
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static int snd_ensoniq_midi_output_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	ensoniq->uartm |= ES_MODE_OUTPUT;
	ensoniq->midi_output = substream;
	if (!(ensoniq->uartm & ES_MODE_INPUT)) {
		outb(ES_CNTRL(3), ES_REG(ensoniq, UART_CONTROL));
		outb(ensoniq->uartc = 0, ES_REG(ensoniq, UART_CONTROL));
		outl(ensoniq->ctrl |= ES_UART_EN, ES_REG(ensoniq, CONTROL));
	}
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static int snd_ensoniq_midi_output_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	if (!(ensoniq->uartm & ES_MODE_INPUT)) {
		outb(ensoniq->uartc = 0, ES_REG(ensoniq, UART_CONTROL));
		outl(ensoniq->ctrl &= ~ES_UART_EN, ES_REG(ensoniq, CONTROL));
	} else {
		outb(ensoniq->uartc &= ~ES_TXINTENM, ES_REG(ensoniq, UART_CONTROL));
	}
	ensoniq->midi_output = NULL;
	ensoniq->uartm &= ~ES_MODE_OUTPUT;
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
	return 0;
}

static void snd_ensoniq_midi_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, substream->rmidi->private_data, return);
	int idx;

	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	if (up) {
		if ((ensoniq->uartc & ES_RXINTEN) == 0) {
			/* empty input FIFO */
			for (idx = 0; idx < 32; idx++)
				inb(ES_REG(ensoniq, UART_DATA));
			ensoniq->uartc |= ES_RXINTEN;
			outb(ensoniq->uartc, ES_REG(ensoniq, UART_CONTROL));
		}
	} else {
		if (ensoniq->uartc & ES_RXINTEN) {
			ensoniq->uartc &= ~ES_RXINTEN;
			outb(ensoniq->uartc, ES_REG(ensoniq, UART_CONTROL));
		}
	}
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
}

static void snd_ensoniq_midi_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, substream->rmidi->private_data, return);
	unsigned char byte;

	spin_lock_irqsave(&ensoniq->reg_lock, flags);
	if (up) {
		if (ES_TXINTENI(ensoniq->uartc) == 0) {
			ensoniq->uartc |= ES_TXINTENO(1);
			/* fill UART FIFO buffer at first, and turn Tx interrupts only if necessary */
			while (ES_TXINTENI(ensoniq->uartc) == 1 &&
			       (inb(ES_REG(ensoniq, UART_STATUS)) & ES_TXRDY)) {
				if (snd_rawmidi_transmit(substream, &byte, 1) != 1) {
					ensoniq->uartc &= ~ES_TXINTENM;
				} else {
					outb(byte, ES_REG(ensoniq, UART_DATA));
				}
			}
			outb(ensoniq->uartc, ES_REG(ensoniq, UART_CONTROL));
		}
	} else {
		if (ES_TXINTENI(ensoniq->uartc) == 1) {
			ensoniq->uartc &= ~ES_TXINTENM;
			outb(ensoniq->uartc, ES_REG(ensoniq, UART_CONTROL));
		}
	}
	spin_unlock_irqrestore(&ensoniq->reg_lock, flags);
}

#ifdef TARGET_OS2
static snd_rawmidi_ops_t snd_ensoniq_midi_output =
{
	        snd_ensoniq_midi_output_open,
	        snd_ensoniq_midi_output_close,
	        snd_ensoniq_midi_output_trigger,
                0
};

static snd_rawmidi_ops_t snd_ensoniq_midi_input =
{
	        snd_ensoniq_midi_input_open,
	        snd_ensoniq_midi_input_close,
	        snd_ensoniq_midi_input_trigger,
                0
};
#else
static snd_rawmidi_ops_t snd_ensoniq_midi_output =
{
	open:           snd_ensoniq_midi_output_open,
	close:          snd_ensoniq_midi_output_close,
	trigger:        snd_ensoniq_midi_output_trigger,
};

static snd_rawmidi_ops_t snd_ensoniq_midi_input =
{
	open:           snd_ensoniq_midi_input_open,
	close:          snd_ensoniq_midi_input_close,
	trigger:        snd_ensoniq_midi_input_trigger,
};
#endif

static int __init snd_ensoniq_midi(ensoniq_t * ensoniq, int device, snd_rawmidi_t **rrawmidi)
{
	snd_rawmidi_t *rmidi;
	int err;

	if (rrawmidi)
		*rrawmidi = NULL;
	if ((err = snd_rawmidi_new(ensoniq->card, "ES1370/1", device, 1, 1, &rmidi)) < 0)
		return err;
#ifdef CHIP1370
	strcpy(rmidi->name, "ES1370");
#else
	strcpy(rmidi->name, "ES1371");
#endif
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_ensoniq_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_ensoniq_midi_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = ensoniq;
	ensoniq->rmidi = rmidi;
	if (rrawmidi)
		*rrawmidi = rmidi;
	return 0;
}

/*
 *  Interrupt handler
 */

static void snd_audiopci_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	ensoniq_t *ensoniq = snd_magic_cast(ensoniq_t, dev_id, return);
	unsigned int status, sctrl;

	if (ensoniq == NULL)
		return;

	status = inl(ES_REG(ensoniq, STATUS));
	if (!(status & ES_INTR))
		return;

	spin_lock(&ensoniq->reg_lock);
	sctrl = ensoniq->sctrl;
	if (status & ES_DAC1)
		sctrl &= ~ES_P1_INT_EN;
	if (status & ES_DAC2)
		sctrl &= ~ES_P2_INT_EN;
	if (status & ES_ADC)
		sctrl &= ~ES_R1_INT_EN;
	outl(sctrl, ES_REG(ensoniq, SERIAL));
	outl(ensoniq->sctrl, ES_REG(ensoniq, SERIAL));
	spin_unlock(&ensoniq->reg_lock);

	if (status & ES_UART)
		snd_ensoniq_midi_interrupt(ensoniq);
	if (status & ES_DAC2)
		snd_pcm_period_elapsed(ensoniq->playback2_substream);
	if (status & ES_ADC)
		snd_pcm_period_elapsed(ensoniq->capture_substream);
	if (status & ES_DAC1)
		snd_pcm_period_elapsed(ensoniq->playback1_substream);
}

static int __init snd_audiopci_probe(struct pci_dev *pci,
				     const struct pci_device_id *id)
{
	static int dev = 0;
	snd_card_t *card;
	ensoniq_t *ensoniq;
	int err, pcm_devs[2];

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

	if ((err = snd_ensoniq_create(card, pci, &ensoniq)) < 0) {
		snd_card_free(card);
		return err;
	}

	pcm_devs[0] = 0; pcm_devs[1] = 1;
#ifdef CHIP1370
	if ((err = snd_ensoniq_1370_mixer(ensoniq)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif
#ifdef CHIP1371
	if ((err = snd_ensoniq_1371_mixer(ensoniq)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif
	if ((err = snd_ensoniq_pcm(ensoniq, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ensoniq_pcm2(ensoniq, 1, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ensoniq_midi(ensoniq, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
#ifdef CHIP1370
	strcpy(card->driver, "ES1370");
#endif
#ifdef CHIP1371
	strcpy(card->driver, "ES1371");
#endif
	strcpy(card->shortname, "Ensoniq AudioPCI");
	sprintf(card->longname, "%s %s at 0x%lx, irq %i",
		card->shortname,
		card->driver,
		ensoniq->port,
		ensoniq->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	PCI_SET_DRIVER_DATA(pci, card);
	dev++;
	return 0;
}

static void __exit snd_audiopci_remove(struct pci_dev *pci)
{
	snd_card_free(PCI_GET_DRIVER_DATA(pci));
	PCI_SET_DRIVER_DATA(pci, NULL);
}

#ifdef TARGET_OS2
static struct pci_driver driver = {
        0, 0,
	"Ensoniq AudioPCI",
	snd_audiopci_ids,
	snd_audiopci_probe,
	snd_audiopci_remove,
        0, 0
};
#else
static struct pci_driver driver = {
	name: "Ensoniq AudioPCI",
	id_table: snd_audiopci_ids,
	probe: snd_audiopci_probe,
	remove: snd_audiopci_remove,
};
#endif
	
static int __init alsa_card_ens137x_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		snd_printk("Ensoniq AudioPCI soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_ens137x_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_ens137x_init)
module_exit(alsa_card_ens137x_exit)

#ifndef MODULE

/* format is: snd-card-ens1370=snd_enable,snd_index,snd_id */

static int __init alsa_card_ens137x_setup(char *str)
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

#if defined(CHIP1370)
__setup("snd-card-ens1370=", alsa_card_ens137x_setup);
#elif defined(CHIP1371)
__setup("snd-card-ens1371=", alsa_card_ens137x_setup);
#endif

#endif /* ifndef MODULE */
