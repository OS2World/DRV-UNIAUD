/*
 *  Driver for Cirrus Logic CS4281 based PCI soundcard
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>,
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
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/ac97_codec.h>
#include <sound/opl3.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#ifndef LINUX_2_2
#include <linux/gameport.h>
#endif

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Cirrus Logic CS4281");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Cirrus Logic,CS4281}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable switches */
static int snd_dual_codec[SNDRV_CARDS];	/* dual codec */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for CS4281 soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for CS4281 soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable CS4281 soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_dual_codec, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dual_codec, "Secondary Codec ID (0 = disabled).");
MODULE_PARM_SYNTAX(snd_dual_codec, SNDRV_ENABLED ",allows:{{0,3}}");

/*
 *
 */

#ifndef PCI_VENDOR_ID_CIRRUS
#define PCI_VENDOR_ID_CIRRUS            0x1013
#endif
#ifndef PCI_DEVICE_ID_CIRRUS_4281
#define PCI_DEVICE_ID_CIRRUS_4281	0x6005
#endif

/*
 *  Direct registers
 */

#define CS4281_BA0_SIZE		0x1000
#define CS4281_BA1_SIZE		0x10000

/*
 *  BA0 registers
 */
#define BA0_HISR		0x0000	/* Host Interrupt Status Register */
#define BA0_HISR_INTENA		(1<<31)	/* Internal Interrupt Enable Bit */
#define BA0_HISR_MIDI		(1<<22)	/* MIDI port interrupt */
#define BA0_HISR_FIFOI		(1<<20)	/* FIFO polled interrupt */
#define BA0_HISR_DMAI		(1<<18)	/* DMA interrupt (half or end) */
#define BA0_HISR_FIFO(c)	(1<<(12+(c))) /* FIFO channel interupt */
#define BA0_HISR_DMA(c)		(1<<(8+(c)))  /* DMA channel interrupt */
#define BA0_HISR_GPPI		(1<<5)	/* General Purpose Input (Primary chip) */
#define BA0_HISR_GPSI		(1<<4)	/* General Purpose Input (Secondary chip) */
#define BA0_HISR_GP3I		(1<<3)	/* GPIO3 pin Interrupt */
#define BA0_HISR_GP1I		(1<<2)	/* GPIO1 pin Interrupt */
#define BA0_HISR_VUPI		(1<<1)	/* VOLUP pin Interrupt */
#define BA0_HISR_VDNI		(1<<0)	/* VOLDN pin Interrupt */

#define BA0_HICR		0x0008	/* Host Interrupt Control Register */
#define BA0_HICR_CHGM		(1<<1)	/* INTENA Change Mask */
#define BA0_HICR_IEV		(1<<0)	/* INTENA Value */
#define BA0_HICR_EOI		(3<<0)	/* End of Interrupt command */

#define BA0_HIMR		0x000c	/* Host Interrupt Mask Register */
					/* Use same contants as for BA0_HISR */

#define BA0_IIER		0x0010	/* ISA Interrupt Enable Register */

#define BA0_HDSR0		0x00f0	/* Host DMA Engine 0 Status Register */
#define BA0_HDSR1		0x00f4	/* Host DMA Engine 1 Status Register */
#define BA0_HDSR2		0x00f8	/* Host DMA Engine 2 Status Register */
#define BA0_HDSR3		0x00fc	/* Host DMA Engine 3 Status Register */

#define BA0_HDSR_CH1P		(1<<25)	/* Channel 1 Pending */
#define BA0_HDSR_CH2P		(1<<24)	/* Channel 2 Pending */
#define BA0_HDSR_DHTC		(1<<17)	/* DMA Half Terminal Count */
#define BA0_HDSR_DTC		(1<<16)	/* DMA Terminal Count */
#define BA0_HDSR_DRUN		(1<<15)	/* DMA Running */
#define BA0_HDSR_RQ		(1<<7)	/* Pending Request */

#define BA0_DCA0		0x0110	/* Host DMA Engine 0 Current Address */
#define BA0_DCC0		0x0114	/* Host DMA Engine 0 Current Count */
#define BA0_DBA0		0x0118	/* Host DMA Engine 0 Base Address */
#define BA0_DBC0		0x011c	/* Host DMA Engine 0 Base Count */
#define BA0_DCA1		0x0120	/* Host DMA Engine 1 Current Address */
#define BA0_DCC1		0x0124	/* Host DMA Engine 1 Current Count */
#define BA0_DBA1		0x0128	/* Host DMA Engine 1 Base Address */
#define BA0_DBC1		0x012c	/* Host DMA Engine 1 Base Count */
#define BA0_DCA2		0x0130	/* Host DMA Engine 2 Current Address */
#define BA0_DCC2		0x0134	/* Host DMA Engine 2 Current Count */
#define BA0_DBA2		0x0138	/* Host DMA Engine 2 Base Address */
#define BA0_DBC2		0x013c	/* Host DMA Engine 2 Base Count */
#define BA0_DCA3		0x0140	/* Host DMA Engine 3 Current Address */
#define BA0_DCC3		0x0144	/* Host DMA Engine 3 Current Count */
#define BA0_DBA3		0x0148	/* Host DMA Engine 3 Base Address */
#define BA0_DBC3		0x014c	/* Host DMA Engine 3 Base Count */
#define BA0_DMR0		0x0150	/* Host DMA Engine 0 Mode */
#define BA0_DCR0		0x0154	/* Host DMA Engine 0 Command */
#define BA0_DMR1		0x0158	/* Host DMA Engine 1 Mode */
#define BA0_DCR1		0x015c	/* Host DMA Engine 1 Command */
#define BA0_DMR2		0x0160	/* Host DMA Engine 2 Mode */
#define BA0_DCR2		0x0164	/* Host DMA Engine 2 Command */
#define BA0_DMR3		0x0168	/* Host DMA Engine 3 Mode */
#define BA0_DCR3		0x016c	/* Host DMA Engine 3 Command */

#define BA0_DMR_DMA		(1<<29)	/* Enable DMA mode */
#define BA0_DMR_POLL		(1<<28)	/* Enable poll mode */
#define BA0_DMR_TBC		(1<<25)	/* Transfer By Channel */
#define BA0_DMR_CBC		(1<<24)	/* Count By Channel (0 = frame resolution) */
#define BA0_DMR_SWAPC		(1<<22)	/* Swap Left/Right Channels */
#define BA0_DMR_SIZE20		(1<<20)	/* Sample is 20-bit */
#define BA0_DMR_USIGN		(1<<19)	/* Unsigned */
#define BA0_DMR_BEND		(1<<18)	/* Big Endian */
#define BA0_DMR_MONO		(1<<17)	/* Mono */
#define BA0_DMR_SIZE8		(1<<16)	/* Sample is 8-bit */
#define BA0_DMR_TYPE_DEMAND	(0<<6)
#define BA0_DMR_TYPE_SINGLE	(1<<6)
#define BA0_DMR_TYPE_BLOCK	(2<<6)
#define BA0_DMR_TYPE_CASCADE	(3<<6)	/* Not supported */
#define BA0_DMR_DEC		(1<<5)	/* Access Increment (0) or Decrement (1) */
#define BA0_DMR_AUTO		(1<<4)	/* Auto-Initialize */
#define BA0_DMR_TR_VERIFY	(0<<2)	/* Verify Transfer */
#define BA0_DMR_TR_WRITE	(1<<2)	/* Write Transfer */
#define BA0_DMR_TR_READ		(2<<2)	/* Read Transfer */

#define BA0_DCR_HTCIE		(1<<17)	/* Half Terminal Count Interrupt */
#define BA0_DCR_TCIE		(1<<16)	/* Terminal Count Interrupt */
#define BA0_DCR_MSK		(1<<0)	/* DMA Mask bit */

#define BA0_FCR0		0x0180	/* FIFO Control 0 */
#define BA0_FCR1		0x0184	/* FIFO Control 1 */
#define BA0_FCR2		0x0188	/* FIFO Control 2 */
#define BA0_FCR3		0x018c	/* FIFO Control 3 */

#define BA0_FCR_FEN		(1<<31)	/* FIFO Enable bit */
#define BA0_FCR_DACZ		(1<<30)	/* DAC Zero */
#define BA0_FCR_PSH		(1<<29)	/* Previous Sample Hold */
#define BA0_FCR_RS(x)		(((x)&0x1f)<<24) /* Right Slot Mapping */
#define BA0_FCR_LS(x)		(((x)&0x1f)<<16) /* Left Slot Mapping */
#define BA0_FCR_SZ(x)		(((x)&0x7f)<<8)	/* FIFO buffer size (in samples) */
#define BA0_FCR_OF(x)		(((x)&0x7f)<<0)	/* FIFO starting offset (in samples) */

#define BA0_FPDR0		0x0190	/* FIFO Polled Data 0 */
#define BA0_FPDR1		0x0194	/* FIFO Polled Data 1 */
#define BA0_FPDR2		0x0198	/* FIFO Polled Data 2 */
#define BA0_FPDR3		0x019c	/* FIFO Polled Data 3 */

#define BA0_FCHS		0x020c	/* FIFO Channel Status */
#define BA0_FCHS_RCO(x)		(1<<(7+(((x)&3)<<3))) /* Right Channel Out */
#define BA0_FCHS_LCO(x)		(1<<(6+(((x)&3)<<3))) /* Left Channel Out */
#define BA0_FCHS_MRP(x)		(1<<(5+(((x)&3)<<3))) /* Move Read Pointer */
#define BA0_FCHS_FE(x)		(1<<(4+(((x)&3)<<3))) /* FIFO Empty */
#define BA0_FCHS_FF(x)		(1<<(3+(((x)&3)<<3))) /* FIFO Full */
#define BA0_FCHS_IOR(x)		(1<<(2+(((x)&3)<<3))) /* Internal Overrun Flag */
#define BA0_FCHS_RCI(x)		(1<<(1+(((x)&3)<<3))) /* Right Channel In */
#define BA0_FCHS_LCI(x)		(1<<(0+(((x)&3)<<3))) /* Left Channel In */

#define BA0_FSIC0		0x0210	/* FIFO Status and Interrupt Control 0 */
#define BA0_FSIC1		0x0214	/* FIFO Status and Interrupt Control 1 */
#define BA0_FSIC2		0x0218	/* FIFO Status and Interrupt Control 2 */
#define BA0_FSIC3		0x021c	/* FIFO Status and Interrupt Control 3 */

#define BA0_FSIC_FIC(x)		(((x)&0x7f)<<24) /* FIFO Interrupt Count */
#define BA0_FSIC_FORIE		(1<<23) /* FIFO OverRun Interrupt Enable */
#define BA0_FSIC_FURIE		(1<<22) /* FIFO UnderRun Interrupt Enable */
#define BA0_FSIC_FSCIE		(1<<16)	/* FIFO Sample Count Interrupt Enable */
#define BA0_FSIC_FSC(x)		(((x)&0x7f)<<8) /* FIFO Sample Count */
#define BA0_FSIC_FOR		(1<<7)	/* FIFO OverRun */
#define BA0_FSIC_FUR		(1<<6)	/* FIFO UnderRun */
#define BA0_FSIC_FSCR		(1<<0)	/* FIFO Sample Count Reached */

#define BA0_PMCS		0x0344	/* Power Management Control/Status */
#define BA0_CWPR		0x03e0	/* Configuration Write Protect */
#define BA0_EPPMC		0x03e4	/* Extended PCI Power Management Control */
#define BA0_GPIOR		0x03e8	/* GPIO Pin Interface Register */

#define BA0_SPMC		0x03ec	/* Serial Port Power Management Control (& ASDIN2 enable) */
#define BA0_SPMC_GIPPEN		(1<<15)	/* GP INT Primary PME# Enable */
#define BA0_SPMC_GISPEN		(1<<14)	/* GP INT Secondary PME# Enable */
#define BA0_SPMC_EESPD		(1<<9)	/* EEPROM Serial Port Disable */
#define BA0_SPMC_ASDI2E		(1<<8)	/* ASDIN2 Enable */
#define BA0_SPMC_ASDO		(1<<7)	/* Asynchronous ASDOUT Assertion */
#define BA0_SPMC_WUP2		(1<<3)	/* Wakeup for Secondary Input */
#define BA0_SPMC_WUP1		(1<<2)	/* Wakeup for Primary Input */
#define BA0_SPMC_ASYNC		(1<<1)	/* Asynchronous ASYNC Assertion */
#define BA0_SPMC_RSTN		(1<<0)	/* Reset Not! */

#define BA0_CFLR		0x03f0	/* Configuration Load Register (EEPROM or BIOS) */
#define BA0_CFLR_DEFAULT	0x00000001 /* CFLR must be in AC97 link mode */
#define BA0_IISR		0x03f4	/* ISA Interrupt Select */
#define BA0_TMS			0x03f8	/* Test Register */
#define BA0_SSVID		0x03fc	/* Subsystem ID register */

#define BA0_CLKCR1		0x0400	/* Clock Control Register 1 */
#define BA0_CLKCR1_CLKON	(1<<25)	/* Read Only */
#define BA0_CLKCR1_DLLRDY	(1<<24)	/* DLL Ready */
#define BA0_CLKCR1_DLLOS	(1<<6)	/* DLL Output Select */
#define BA0_CLKCR1_SWCE		(1<<5)	/* Clock Enable */
#define BA0_CLKCR1_DLLP		(1<<4)	/* DLL PowerUp */
#define BA0_CLKCR1_DLLSS	(((x)&3)<<3) /* DLL Source Select */

#define BA0_FRR			0x0410	/* Feature Reporting Register */
#define BA0_SLT12O		0x041c	/* Slot 12 GPIO Output Register for AC-Link */

#define BA0_SERMC		0x0420	/* Serial Port Master Control */
#define BA0_SERMC_FCRN		(1<<27)	/* Force Codec Ready Not */
#define BA0_SERMC_ODSEN2	(1<<25)	/* On-Demand Support Enable ASDIN2 */
#define BA0_SERMC_ODSEN1	(1<<24)	/* On-Demand Support Enable ASDIN1 */
#define BA0_SERMC_SXLB		(1<<21)	/* ASDIN2 to ASDOUT Loopback */
#define BA0_SERMC_SLB		(1<<20)	/* ASDOUT to ASDIN2 Loopback */
#define BA0_SERMC_LOVF		(1<<19)	/* Loopback Output Valid Frame bit */
#define BA0_SERMC_TCID(x)	(((x)&3)<<16) /* Target Secondary Codec ID */
#define BA0_SERMC_PXLB		(5<<1)	/* Primary Port External Loopback */
#define BA0_SERMC_PLB		(4<<1)	/* Primary Port Internal Loopback */
#define BA0_SERMC_PTC		(7<<1)	/* Port Timing Configuration */
#define BA0_SERMC_PTC_AC97	(1<<1)	/* AC97 mode */
#define BA0_SERMC_MSPE		(1<<0)	/* Master Serial Port Enable */

#define BA0_SERC1		0x0428	/* Serial Port Configuration 1 */
#define BA0_SERC1_SO1F(x)	(((x)&7)>>1) /* Primary Output Port Format */
#define BA0_SERC1_AC97		(1<<1)
#define BA0_SERC1_SO1EN		(1<<0)	/* Primary Output Port Enable */

#define BA0_SERC2		0x042c	/* Serial Port Configuration 2 */
#define BA0_SERC2_SI1F(x)	(((x)&7)>>1) /* Primary Input Port Format */
#define BA0_SERC2_AC97		(1<<1)
#define BA0_SERC2_SI1EN		(1<<0)	/* Primary Input Port Enable */

#define BA0_SLT12M		0x045c	/* Slot 12 Monitor Register for Primary AC-Link */

#define BA0_ACCTL		0x0460	/* AC'97 Control */
#define BA0_ACCTL_TC		(1<<6)	/* Target Codec */
#define BA0_ACCTL_CRW		(1<<4)	/* 0=Write, 1=Read Command */
#define BA0_ACCTL_DCV		(1<<3)	/* Dynamic Command Valid */
#define BA0_ACCTL_VFRM		(1<<2)	/* Valid Frame */
#define BA0_ACCTL_ESYN		(1<<1)	/* Enable Sync */

#define BA0_ACSTS		0x0464	/* AC'97 Status */
#define BA0_ACSTS_VSTS		(1<<1)	/* Valid Status */
#define BA0_ACSTS_CRDY		(1<<0)	/* Codec Ready */

#define BA0_ACOSV		0x0468	/* AC'97 Output Slot Valid */
#define BA0_ACOSV_SLV(x)	(1<<((x)-3))

#define BA0_ACCAD		0x046c	/* AC'97 Command Address */
#define BA0_ACCDA		0x0470	/* AC'97 Command Data */

#define BA0_ACISV		0x0474	/* AC'97 Input Slot Valid */
#define BA0_ACISV_SLV(x)	(1<<((x)-3))

#define BA0_ACSAD		0x0478	/* AC'97 Status Address */
#define BA0_ACSDA		0x047c	/* AC'97 Status Data */
#define BA0_JSPT		0x0480	/* Joystick poll/trigger */
#define BA0_JSCTL		0x0484	/* Joystick control */
#define BA0_JSC1		0x0488	/* Joystick control */
#define BA0_JSC2		0x048c	/* Joystick control */
#define BA0_JSIO		0x04a0

#define BA0_MIDCR		0x0490	/* MIDI Control */
#define BA0_MIDCR_MRST		(1<<5)	/* Reset MIDI Interface */
#define BA0_MIDCR_MLB		(1<<4)	/* MIDI Loop Back Enable */
#define BA0_MIDCR_TIE		(1<<3)	/* MIDI Transmuit Interrupt Enable */
#define BA0_MIDCR_RIE		(1<<2)	/* MIDI Receive Interrupt Enable */
#define BA0_MIDCR_RXE		(1<<1)	/* MIDI Receive Enable */
#define BA0_MIDCR_TXE		(1<<0)	/* MIDI Transmit Enable */

#define BA0_MIDCMD		0x0494	/* MIDI Command (wo) */

#define BA0_MIDSR		0x0494	/* MIDI Status (ro) */
#define BA0_MIDSR_RDA		(1<<15)	/* Sticky bit (RBE 1->0) */
#define BA0_MIDSR_TBE		(1<<14) /* Sticky bit (TBF 0->1) */
#define BA0_MIDSR_RBE		(1<<7)	/* Receive Buffer Empty */
#define BA0_MIDSR_TBF		(1<<6)	/* Transmit Buffer Full */

#define BA0_MIDWP		0x0498	/* MIDI Write */
#define BA0_MIDRP		0x049c	/* MIDI Read (ro) */

#define BA0_AODSD1		0x04a8	/* AC'97 On-Demand Slot Disable for primary link (ro) */
#define BA0_AODSD1_NDS(x)	(1<<((x)-3))

#define BA0_AODSD2		0x04ac	/* AC'97 On-Demand Slot Disable for secondary link (ro) */
#define BA0_AODSD2_NDS(x)	(1<<((x)-3))

#define BA0_CFGI		0x04b0	/* Configure Interface (EEPROM interface) */
#define BA0_SLT12M2		0x04dc	/* Slot 12 Monitor Register 2 for secondary AC-link */
#define BA0_ACSTS2		0x04e4	/* AC'97 Status Register 2 */
#define BA0_ACISV2		0x04f4	/* AC'97 Input Slot Valid Register 2 */
#define BA0_ACSAD2		0x04f8	/* AC'97 Status Address Register 2 */
#define BA0_ACSDA2		0x04fc	/* AC'97 Status Data Register 2 */
#define BA0_FMSR		0x0730	/* FM Synthesis Status (ro) */
#define BA0_B0AP		0x0730	/* FM Bank 0 Address Port (wo) */
#define BA0_FMDP		0x0734	/* FM Data Port */
#define BA0_B1AP		0x0738	/* FM Bank 1 Address Port */
#define BA0_B1DP		0x073c	/* FM Bank 1 Data Port */

#define BA0_SSPM		0x0740	/* Sound System Power Management */
#define BA0_SSPM_MIXEN		(1<<6)	/* Playback SRC + FM/Wavetable MIX */
#define BA0_SSPM_CSRCEN		(1<<5)	/* Capture Sample Rate Converter Enable */
#define BA0_SSPM_PSRCEN		(1<<4)	/* Playback Sample Rate Converter Enable */
#define BA0_SSPM_JSEN		(1<<3)	/* Joystick Enable */
#define BA0_SSPM_ACLEN		(1<<2)	/* Serial Port Engine and AC-Link Enable */
#define BA0_SSPM_FMEN		(1<<1)	/* FM Synthesis Block Enable */

#define BA0_DACSR		0x0744	/* DAC Sample Rate - Playback SRC */
#define BA0_ADCSR		0x0748	/* ADC Sample Rate - Capture SRC */

#define BA0_SSCR		0x074c	/* Sound System Control Register */
#define BA0_SSCR_HVS1		(1<<23)	/* Hardwave Volume Step (0=1,1=2) */
#define BA0_SSCR_MVCS		(1<<19)	/* Master Volume Codec Select */
#define BA0_SSCR_MVLD		(1<<18)	/* Master Volume Line Out Disable */
#define BA0_SSCR_MVAD		(1<<17)	/* Master Volume Alternate Out Disable */
#define BA0_SSCR_MVMD		(1<<16)	/* Master Volume Mono Out Disable */
#define BA0_SSCR_XLPSRC		(1<<8)	/* External SRC Loopback Mode */
#define BA0_SSCR_LPSRC		(1<<7)	/* SRC Loopback Mode */
#define BA0_SSCR_CDTX		(1<<5)	/* CD Transfer Data */
#define BA0_SSCR_HVC		(1<<3)	/* Harware Volume Control Enable */

#define BA0_FMLVC		0x0754	/* FM Synthesis Left Volume Control */
#define BA0_FMRVC		0x0758	/* FM Synthesis Right Volume Control */
#define BA0_SRCSA		0x075c	/* SRC Slot Assignments */
#define BA0_PPLVC		0x0760	/* PCM Playback Left Volume Control */
#define BA0_PPRVC		0x0764	/* PCM Playback Right Volume Control */

/* Source Slot Numbers - Playback */
#define SRCSLOT_LEFT_PCM_PLAYBACK		0
#define SRCSLOT_RIGHT_PCM_PLAYBACK		1
#define SRCSLOT_PHONE_LINE_1_DAC		2
#define SRCSLOT_CENTER_PCM_PLAYBACK		3
#define SRCSLOT_LEFT_SURROUND_PCM_PLAYBACK	4
#define SRCSLOT_RIGHT_SURROUND_PCM_PLAYBACK	5
#define SRCSLOT_LFE_PCM_PLAYBACK		6
#define SRCSLOT_PHONE_LINE_2_DAC		7
#define SRCSLOT_HEADSET_DAC			8
#define SRCSLOT_LEFT_WT				29  /* invalid for BA0_SRCSA */
#define SRCSLOT_RIGHT_WT			30  /* invalid for BA0_SRCSA */

/* Source Slot Numbers - Capture */
#define SRCSLOT_LEFT_PCM_RECORD			10
#define SRCSLOT_RIGHT_PCM_RECORD		11
#define SRCSLOT_PHONE_LINE_1_ADC		12
#define SRCSLOT_MIC_ADC				13
#define SRCSLOT_PHONE_LINE_2_ADC		17
#define SRCSLOT_HEADSET_ADC			18
#define SRCSLOT_SECONDARY_LEFT_PCM_RECORD	20
#define SRCSLOT_SECONDARY_RIGHT_PCM_RECORD	21
#define SRCSLOT_SECONDARY_PHONE_LINE_1_ADC	22
#define SRCSLOT_SECONDARY_MIC_ADC		23
#define SRCSLOT_SECONDARY_PHONE_LINE_2_ADC	27
#define SRCSLOT_SECONDARY_HEADSET_ADC		28

/* Source Slot Numbers - Others */
#define SRCSLOT_POWER_DOWN			31

/* MIDI modes */
#define CS4281_MODE_OUTPUT		(1<<0)
#define CS4281_MODE_INPUT		(1<<1)

/* joystick bits */
/* Bits for JSPT */
#define JSPT_CAX                                0x00000001
#define JSPT_CAY                                0x00000002
#define JSPT_CBX                                0x00000004
#define JSPT_CBY                                0x00000008
#define JSPT_BA1                                0x00000010
#define JSPT_BA2                                0x00000020
#define JSPT_BB1                                0x00000040
#define JSPT_BB2                                0x00000080

/* Bits for JSCTL */
#define JSCTL_SP_MASK                           0x00000003
#define JSCTL_SP_SLOW                           0x00000000
#define JSCTL_SP_MEDIUM_SLOW                    0x00000001
#define JSCTL_SP_MEDIUM_FAST                    0x00000002
#define JSCTL_SP_FAST                           0x00000003
#define JSCTL_ARE                               0x00000004

/* Data register pairs masks */
#define JSC1_Y1V_MASK                           0x0000FFFF
#define JSC1_X1V_MASK                           0xFFFF0000
#define JSC1_Y1V_SHIFT                          0
#define JSC1_X1V_SHIFT                          16
#define JSC2_Y2V_MASK                           0x0000FFFF
#define JSC2_X2V_MASK                           0xFFFF0000
#define JSC2_Y2V_SHIFT                          0
#define JSC2_X2V_SHIFT                          16

/* JS GPIO */
#define JSIO_DAX                                0x00000001
#define JSIO_DAY                                0x00000002
#define JSIO_DBX                                0x00000004
#define JSIO_DBY                                0x00000008
#define JSIO_AXOE                               0x00000010
#define JSIO_AYOE                               0x00000020
#define JSIO_BXOE                               0x00000040
#define JSIO_BYOE                               0x00000080

/*
 *
 */

#define chip_t cs4281_t

typedef struct snd_cs4281 cs4281_t;
typedef struct snd_cs4281_dma cs4281_dma_t;

struct snd_cs4281_dma {
	snd_pcm_substream_t *substream;
	unsigned int regDBA;		/* offset to DBA register */
	unsigned int regDCA;		/* offset to DCA register */
	unsigned int regDBC;		/* offset to DBC register */
	unsigned int regDCC;		/* offset to DCC register */
	unsigned int regDMR;		/* offset to DMR register */
	unsigned int regDCR;		/* offset to DCR register */
	unsigned int regHDSR;		/* offset to HDSR register */
	unsigned int regFCR;		/* offset to FCR register */
	unsigned int regFSIC;		/* offset to FSIC register */
	unsigned int valDMR;		/* DMA mode */
	unsigned int valDCR;		/* DMA command */
	unsigned int valFCR;		/* FIFO control */
	unsigned int fifo_offset;	/* FIFO offset within BA1 */
	unsigned char left_slot;	/* FIFO left slot */
	unsigned char right_slot;	/* FIFO right slot */
	int frag;			/* period number */
};

struct snd_cs4281 {
	int irq;

	unsigned long ba0;		/* virtual (accessible) address */
	unsigned long ba1;		/* virtual (accessible) address */
	unsigned long ba0_addr;
	unsigned long ba1_addr;
	struct resource *ba0_res;
	struct resource *ba1_res;

	int dual_codec;

	ac97_t *ac97;
	ac97_t *ac97_secondary;

	struct pci_dev *pci;
	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *midi_input;
	snd_rawmidi_substream_t *midi_output;

	cs4281_dma_t dma[4];

	unsigned char src_left_play_slot;
	unsigned char src_right_play_slot;
	unsigned char src_left_rec_slot;
	unsigned char src_right_rec_slot;

	unsigned int spurious_dhtc_irq;
	unsigned int spurious_dtc_irq;

	void *proc_entry_BA0;
	void *proc_entry_BA1;

	spinlock_t reg_lock;
	unsigned int midcr;
	unsigned int uartm;
	snd_info_entry_t *proc_entry;

	struct snd_cs4281_gameport *gameport;
};

static void snd_cs4281_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static struct pci_device_id snd_cs4281_ids[] __devinitdata = {
	{ 0x1013, 0x6005, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* CS4281 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_cs4281_ids);

/*
 *  constants
 */
#define CS4281_FIFO_SIZE	32

/*
 *  common I/O routines
 */

static void snd_cs4281_delay(unsigned int delay)
{
	if (delay > 999) {
		signed long end_time;
		delay = (delay * HZ) / 1000000;
		if (delay < 1)
			delay = 1;
		end_time = jiffies + delay;
		do {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
	        } while (end_time - (signed long)jiffies >= 0);
	} else {
		udelay(delay);
	}
}

static inline void snd_cs4281_pokeBA0(cs4281_t *chip, unsigned long offset, unsigned int val)
{
        writel(val, chip->ba0 + offset);
}

static inline unsigned int snd_cs4281_peekBA0(cs4281_t *chip, unsigned long offset)
{
        return readl(chip->ba0 + offset);
}

static void snd_cs4281_ac97_write(ac97_t *ac97,
				   unsigned short reg, unsigned short val)
{
	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97
	 *  3. Write ACCTL = Control Register = 460h for initiating the write
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 07h
	 *  5. if DCV not cleared, break and return error
	 */
	cs4281_t *chip = snd_magic_cast(cs4281_t, ac97->private_data, return);
	int count;

	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  reset CRW - Write command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
         */
	snd_cs4281_pokeBA0(chip, BA0_ACCAD, reg);
	snd_cs4281_pokeBA0(chip, BA0_ACCDA, val);
	snd_cs4281_pokeBA0(chip, BA0_ACCTL, BA0_ACCTL_DCV | BA0_ACCTL_VFRM |
				            BA0_ACCTL_ESYN | (ac97->num ? BA0_ACCTL_TC : 0));
	for (count = 0; count < 2000; count++) {
		/*
		 *  First, we want to wait for a short time.
		 */
		udelay(10);
		/*
		 *  Now, check to see if the write has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 07h
		 */
		if (!(snd_cs4281_peekBA0(chip, BA0_ACCTL) & BA0_ACCTL_DCV)) {
			return;
		}
	}
	snd_printk(KERN_ERR "AC'97 write problem, reg = 0x%x, val = 0x%x\n", reg, val);
}

static unsigned short snd_cs4281_ac97_read(ac97_t *ac97,
					    unsigned short reg)
{
	cs4281_t *chip = snd_magic_cast(cs4281_t, ac97->private_data, return -ENXIO);
	int count;
	unsigned short result;

	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97 
	 *  3. Write ACCTL = Control Register = 460h for initiating the write
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 17h
	 *  5. if DCV not cleared, break and return error
	 *  6. Read ACSTS = Status Register = 464h, check VSTS bit
	 */

	snd_cs4281_peekBA0(chip, ac97->num ? BA0_ACSDA2 : BA0_ACSDA);

	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  set CRW - Read command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
	 */

	snd_cs4281_pokeBA0(chip, BA0_ACCAD, reg);
	snd_cs4281_pokeBA0(chip, BA0_ACCDA, 0);
	snd_cs4281_pokeBA0(chip, BA0_ACCTL, BA0_ACCTL_DCV | BA0_ACCTL_CRW |
					    BA0_ACCTL_VFRM | BA0_ACCTL_ESYN |
			   (ac97->num ? BA0_ACCTL_TC : 0));


	/*
	 *  Wait for the read to occur.
	 */
	for (count = 0; count < 500; count++) {
		/*
		 *  First, we want to wait for a short time.
	 	 */
		udelay(10);
		/*
		 *  Now, check to see if the read has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 17h
		 */
		if (!(snd_cs4281_peekBA0(chip, BA0_ACCTL) & BA0_ACCTL_DCV))
			goto __ok1;
	}

	snd_printk(KERN_ERR "AC'97 read problem (ACCTL_DCV), reg = 0x%x\n", reg);
	result = 0xffff;
	goto __end;
	
      __ok1:
	/*
	 *  Wait for the valid status bit to go active.
	 */
	for (count = 0; count < 100; count++) {
		/*
		 *  Read the AC97 status register.
		 *  ACSTS = Status Register = 464h
		 *  VSTS - Valid Status
		 */
		if (snd_cs4281_peekBA0(chip, ac97->num ? BA0_ACSTS2 : BA0_ACSTS) & BA0_ACSTS_VSTS)
			goto __ok2;
		udelay(10);
	}
	
	snd_printk(KERN_ERR "AC'97 read problem (ACSTS_VSTS), reg = 0x%x\n", reg);
	result = 0xffff;
	goto __end;

      __ok2:
	/*
	 *  Read the data returned from the AC97 register.
	 *  ACSDA = Status Data Register = 474h
	 */
	result = snd_cs4281_peekBA0(chip, ac97->num ? BA0_ACSDA2 : BA0_ACSDA);

      __end:
	return result;
}

/*
 *  PCM part
 */

static int snd_cs4281_trigger(snd_pcm_substream_t *substream, int cmd)
{
	cs4281_dma_t *dma = (cs4281_dma_t *)substream->runtime->private_data;
	cs4281_t *chip = snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dma->valDCR |= BA0_DCR_MSK;
		dma->valFCR |= BA0_FCR_FEN;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dma->valDCR &= ~BA0_DCR_MSK;
		dma->valFCR &= ~BA0_FCR_FEN;
		break;
	case SNDRV_PCM_TRIGGER_START:
		snd_cs4281_pokeBA0(chip, dma->regDMR, dma->valDMR & ~BA0_DMR_DMA);
		dma->valDMR |= BA0_DMR_DMA;
		dma->valDCR &= ~BA0_DCR_MSK;
		dma->valFCR |= BA0_FCR_FEN;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dma->valDMR &= ~(BA0_DMR_DMA|BA0_DMR_POLL);
		dma->valDCR |= BA0_DCR_MSK;
#ifdef TARGET_OS2
                /* Leave wave playback FIFO enabled for FM */
                if(dma->regFCR != BA0_FCR0)
  		    dma->valFCR &= ~BA0_FCR_FEN;
#endif
		break;
	default:
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return -EINVAL;
	}
	snd_cs4281_pokeBA0(chip, dma->regDMR, dma->valDMR);
	snd_cs4281_pokeBA0(chip, dma->regFCR, dma->valFCR);
	snd_cs4281_pokeBA0(chip, dma->regDCR, dma->valDCR);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static unsigned int snd_cs4281_rate(unsigned int rate, unsigned int *real_rate)
{
	unsigned int val = ~0;
	
	if (real_rate)
		*real_rate = rate;
	/* special "hardcoded" rates */
	switch (rate) {
	case 8000:	return 5;
	case 11025:	return 4;
	case 16000:	return 3;
	case 22050:	return 2;
	case 44100:	return 1;
	case 48000:	return 0;
	default:
		goto __variable;
	}
      __variable:
	val = 1536000 / rate;
	if (real_rate)
		*real_rate = 1536000 / val;
	return val;
}

static void snd_cs4281_mode(cs4281_t *chip, cs4281_dma_t *dma, snd_pcm_runtime_t *runtime, int capture, int src)
{
	int rec_mono;

	dma->valDMR = BA0_DMR_TYPE_SINGLE | BA0_DMR_AUTO |
		      (capture ? BA0_DMR_TR_WRITE : BA0_DMR_TR_READ);
	if (runtime->channels == 1)
		dma->valDMR |= BA0_DMR_MONO;
	if (snd_pcm_format_unsigned(runtime->format) > 0)
		dma->valDMR |= BA0_DMR_USIGN;
	if (snd_pcm_format_big_endian(runtime->format) > 0)
		dma->valDMR |= BA0_DMR_BEND;
	switch (snd_pcm_format_width(runtime->format)) {
	case 8: dma->valDMR |= BA0_DMR_SIZE8;
		if (runtime->channels == 1)
			dma->valDMR |= BA0_DMR_SWAPC;
		break;
	case 32: dma->valDMR |= BA0_DMR_SIZE20; break;
	}
	dma->frag = 0;	/* for workaround */
	dma->valDCR = BA0_DCR_TCIE | BA0_DCR_MSK;
	if (runtime->buffer_size != runtime->period_size)
		dma->valDCR |= BA0_DCR_HTCIE;
	/* Initialize DMA */
	snd_cs4281_pokeBA0(chip, dma->regDBA, runtime->dma_addr);
	snd_cs4281_pokeBA0(chip, dma->regDBC, runtime->buffer_size - 1);
	rec_mono = (chip->dma[1].valDMR & BA0_DMR_MONO) == BA0_DMR_MONO;
	snd_cs4281_pokeBA0(chip, BA0_SRCSA, (chip->src_left_play_slot << 0) |
					    (chip->src_right_play_slot << 8) |
					    (chip->src_left_rec_slot << 16) |
					    ((rec_mono ? 31 : chip->src_right_rec_slot) << 24));
	if (!src)
		goto __skip_src;
	if (!capture) {
		if (dma->left_slot == chip->src_left_play_slot) {
			unsigned int val = snd_cs4281_rate(runtime->rate, NULL);
			snd_assert(dma->right_slot == chip->src_right_play_slot, );
			snd_cs4281_pokeBA0(chip, BA0_DACSR, val);
		}
	} else {
		if (dma->left_slot == chip->src_left_rec_slot) {
			unsigned int val = snd_cs4281_rate(runtime->rate, NULL);
			snd_assert(dma->right_slot == chip->src_right_rec_slot, );
			snd_cs4281_pokeBA0(chip, BA0_ADCSR, val);
		}
	}
      __skip_src:
	/* Initialize FIFO */
#ifdef TARGET_OS2
        /* Deactivate wave playback FIFO before changing slot assignments */
        if(dma->regFCR == BA0_FCR0)
  	    snd_cs4281_pokeBA0(chip, dma->regFCR, snd_cs4281_peekBA0(chip, dma->regFCR) & ~BA0_FCR_FEN);
#endif
	dma->valFCR = BA0_FCR_LS(dma->left_slot) |
		      BA0_FCR_RS(capture && (dma->valDMR & BA0_DMR_MONO) ? 31 : dma->right_slot) |
		      BA0_FCR_SZ(CS4281_FIFO_SIZE) |
		      BA0_FCR_OF(dma->fifo_offset);
	snd_cs4281_pokeBA0(chip, dma->regFCR, dma->valFCR | (capture ? BA0_FCR_PSH : 0));

#ifdef TARGET_OS2
        /* Activate FIFO again for FM playback */
        if(dma->regFCR == BA0_FCR0)
 	    snd_cs4281_pokeBA0(chip, dma->regFCR, dma->valFCR | BA0_FCR_FEN);
#endif

	/* Clear FIFO Status and Interrupt Control Register */
	snd_cs4281_pokeBA0(chip, dma->regFSIC, 0);
}

static int snd_cs4281_hw_params(snd_pcm_substream_t * substream,
				snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_cs4281_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_cs4281_playback_prepare(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	cs4281_dma_t *dma = (cs4281_dma_t *)runtime->private_data;
	cs4281_t *chip = snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs4281_mode(chip, dma, runtime, 0, 1);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cs4281_capture_prepare(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	cs4281_dma_t *dma = (cs4281_dma_t *)runtime->private_data;
	cs4281_t *chip = snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs4281_mode(chip, dma, runtime, 1, 1);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static snd_pcm_uframes_t snd_cs4281_pointer(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	cs4281_dma_t *dma = (cs4281_dma_t *)runtime->private_data;
	cs4281_t *chip = snd_pcm_substream_chip(substream);

	// printk("DCC = 0x%x, buffer_size = 0x%x, jiffies = %li\n", snd_cs4281_peekBA0(chip, dma->regDCC), runtime->buffer_size, jiffies);
#ifdef TARGET_OS2
	return runtime->buffer_size -
	       snd_cs4281_peekBA0(chip, dma->regDCC) - 1;
#else
	return runtime->buffer_size -
	       snd_cs4281_peekBA0(chip, dma->regDCC);
#endif
}
#ifdef TARGET_OS2
static snd_pcm_hardware_t snd_cs4281_playback =
{
/*	info:			*/	(SNDRV_PCM_INFO_MMAP |
				 	SNDRV_PCM_INFO_INTERLEAVED |
					 SNDRV_PCM_INFO_MMAP_VALID |
					 SNDRV_PCM_INFO_PAUSE |
					 SNDRV_PCM_INFO_SYNC_START),
/*	formats:		*/	SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
					SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_U16_BE | SNDRV_PCM_FMTBIT_S16_BE |
					SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_S32_LE |
					SNDRV_PCM_FMTBIT_U32_BE | SNDRV_PCM_FMTBIT_S32_BE,
/*	rates:			*/	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:		*/	4000,
/*	rate_max:		*/	48000,
/*	channels_min:		*/	1,
/*	channels_max:		*/	2,
/*	buffer_bytes_max:	*/	(512*1024),
/*	period_bytes_min:	*/	64,
/*	period_bytes_max:	*/	(512*1024),
/*	periods_min:		*/	1,
/*	periods_max:		*/	2,
/*	fifo_size:		*/	CS4281_FIFO_SIZE,
};

static snd_pcm_hardware_t snd_cs4281_capture =
{
/*	info:			*/	(SNDRV_PCM_INFO_MMAP |
					 SNDRV_PCM_INFO_INTERLEAVED |
					 SNDRV_PCM_INFO_MMAP_VALID |
					 SNDRV_PCM_INFO_PAUSE |
					 SNDRV_PCM_INFO_SYNC_START),
/*	formats:		*/	SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_U16_BE | SNDRV_PCM_FMTBIT_S16_BE |
					SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_S32_LE |
					SNDRV_PCM_FMTBIT_U32_BE | SNDRV_PCM_FMTBIT_S32_BE,
/*	rates:			*/	SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
/*	rate_min:		*/	4000,
/*	rate_max:		*/	48000,
/*	channels_min:		*/	1,
/*	channels_max:		*/	2,
/*	buffer_bytes_max:	*/	(512*1024),
/*	period_bytes_min:	*/	64,
/*	period_bytes_max:	*/	(512*1024),
/*	periods_min:		*/	1,
/*	periods_max:		*/	2,
/*	fifo_size:		*/	CS4281_FIFO_SIZE,
};
#else
static snd_pcm_hardware_t snd_cs4281_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_SYNC_START),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_U16_BE | SNDRV_PCM_FMTBIT_S16_BE |
				SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_S32_LE |
				SNDRV_PCM_FMTBIT_U32_BE | SNDRV_PCM_FMTBIT_S32_BE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(512*1024),
	period_bytes_min:	64,
	period_bytes_max:	(512*1024),
	periods_min:		1,
	periods_max:		2,
	fifo_size:		CS4281_FIFO_SIZE,
};

static snd_pcm_hardware_t snd_cs4281_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_SYNC_START),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_U16_BE | SNDRV_PCM_FMTBIT_S16_BE |
				SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_S32_LE |
				SNDRV_PCM_FMTBIT_U32_BE | SNDRV_PCM_FMTBIT_S32_BE,
	rates:			SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	rate_min:		4000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	(512*1024),
	period_bytes_min:	64,
	period_bytes_max:	(512*1024),
	periods_min:		1,
	periods_max:		2,
	fifo_size:		CS4281_FIFO_SIZE,
};
#endif

static int snd_cs4281_playback_open(snd_pcm_substream_t * substream)
{
	cs4281_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	cs4281_dma_t *dma;

	dma = &chip->dma[0];
	dma->substream = substream;
	dma->left_slot = 0;
	dma->right_slot = 1;
	runtime->private_data = dma;
	runtime->hw = snd_cs4281_playback;
	snd_pcm_set_sync(substream);
	/* should be detected from the AC'97 layer, but it seems
	   that although CS4297A rev B reports 18-bit ADC resolution,
	   samples are 20-bit */
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 20);
	return 0;
}

static int snd_cs4281_capture_open(snd_pcm_substream_t * substream)
{
	cs4281_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	cs4281_dma_t *dma;

	dma = &chip->dma[1];
	dma->substream = substream;
	dma->left_slot = 10;
	dma->right_slot = 11;
	runtime->private_data = dma;
	runtime->hw = snd_cs4281_capture;
	snd_pcm_set_sync(substream);
	/* should be detected from the AC'97 layer, but it seems
	   that although CS4297A rev B reports 18-bit ADC resolution,
	   samples are 20-bit */
	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 20);
	return 0;
}

static int snd_cs4281_playback_close(snd_pcm_substream_t * substream)
{
	cs4281_dma_t *dma = (cs4281_dma_t *)substream->runtime->private_data;

	dma->substream = NULL;
	return 0;
}

static int snd_cs4281_capture_close(snd_pcm_substream_t * substream)
{
	cs4281_dma_t *dma = (cs4281_dma_t *)substream->runtime->private_data;

	dma->substream = NULL;
	return 0;
}

#ifdef TARGET_OS2

static int snd_cs4281fm_info_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type              = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count             = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0x1F;
	return 0;
}
 
static int snd_cs4281fm_get_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs4281_t *chip = snd_kcontrol_chip(kcontrol);
	int volR, volL, range = 0x1F;

	volL = range - (snd_cs4281_peekBA0(chip, BA0_FMLVC) & 0x1F);
	volR = range - (snd_cs4281_peekBA0(chip, BA0_FMRVC) & 0x1F);

	ucontrol->value.integer.value[0] = volL;
        ucontrol->value.integer.value[1] = volR;
	return 0;
}

static int snd_cs4281fm_put_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs4281_t *chip = snd_kcontrol_chip(kcontrol);
	int change = 0;
	int volR, volL, range = 0x1F;

	volL = range - (snd_cs4281_peekBA0(chip, BA0_FMLVC) & 0x1F);
	volR = range - (snd_cs4281_peekBA0(chip, BA0_FMRVC) & 0x1F);

        if(ucontrol->value.integer.value[0] != volL) {
  	    volL = range - ucontrol->value.integer.value[0];
            snd_cs4281_pokeBA0(chip, BA0_FMLVC, volL);
            change = 1;
        }
        if(ucontrol->value.integer.value[0] != volL) {
  	    volR = range - ucontrol->value.integer.value[1];
            snd_cs4281_pokeBA0(chip, BA0_FMRVC, volR);
            change = 1;
        }
	return change;
}

static snd_kcontrol_new_t snd_cs4281_fmvol = 
{ SNDRV_CTL_ELEM_IFACE_MIXER, 0, 0, "Synth Playback Volume", 0, 
  0, snd_cs4281fm_info_volume, 
  snd_cs4281fm_get_volume, snd_cs4281fm_put_volume, 
  0
};

int snd_cs4281_playback_ioctl(snd_pcm_substream_t *substream, unsigned int cmd, void *arg)
{
    switch(cmd) {
    case SNDRV_PCM_IOCTL1_SETVOLUME:
    {       
       	snd_pcm_volume_t    *volume = (snd_pcm_volume_t *)arg;
	cs4281_t *chip = snd_pcm_substream_chip(substream);
        int       range = 0x1F;
        int       volL, volR;

        if(volume == NULL || chip == NULL) {
            DebugInt3();
            return -ENOMEM;
        }
        volL = range - (volume->volume[SNDRV_PCM_VOL_FRONT_LEFT] * range)/SNDRV_PCM_VOL_MAX;
        volR = range - (volume->volume[SNDRV_PCM_VOL_FRONT_RIGHT] * range)/SNDRV_PCM_VOL_MAX;

	snd_cs4281_pokeBA0(chip, BA0_PPLVC, volL);
	snd_cs4281_pokeBA0(chip, BA0_PPRVC, volR);
        return 0;        
    }
    case SNDRV_PCM_IOCTL1_GETVOLUME:
    {       
       	snd_pcm_volume_t    *volume = (snd_pcm_volume_t *)arg;
	cs4281_t *chip = snd_pcm_substream_chip(substream);
        int       range = 0x1F;
        int       volL, volR;

        if(volume == NULL || chip == NULL) {
            DebugInt3();
            return -ENOMEM;
        }
	volL = range - (snd_cs4281_peekBA0(chip, BA0_PPLVC) & 0x1F);
	volR = range - (snd_cs4281_peekBA0(chip, BA0_PPRVC) & 0x1F);

        volume->volume[SNDRV_PCM_VOL_FRONT_LEFT]  = (volL * SNDRV_PCM_VOL_MAX) / range;
        volume->volume[SNDRV_PCM_VOL_FRONT_RIGHT] = (volR * SNDRV_PCM_VOL_MAX) / range;

        volume->nrchannels = 2;
        volume->volume[SNDRV_PCM_VOL_REAR_LEFT]   = volume->volume[SNDRV_PCM_VOL_FRONT_LEFT];
        volume->volume[SNDRV_PCM_VOL_REAR_RIGHT]  = volume->volume[SNDRV_PCM_VOL_FRONT_RIGHT];
        return 0;
    }

    }
    return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static snd_pcm_ops_t snd_cs4281_playback_ops = {
/*	open:		*/	snd_cs4281_playback_open,
/*	close:		*/	snd_cs4281_playback_close,
/*	ioctl:		*/	snd_cs4281_playback_ioctl,
/*	hw_params:	*/	snd_cs4281_hw_params,
/*	hw_free:	*/	snd_cs4281_hw_free,
/*	prepare:	*/	snd_cs4281_playback_prepare,
/*	trigger:	*/	snd_cs4281_trigger,
/*	pointer:	*/	snd_cs4281_pointer,
};

static snd_pcm_ops_t snd_cs4281_capture_ops = {
/*	open:		*/	snd_cs4281_capture_open,
/*	close:		*/	snd_cs4281_capture_close,
/*	ioctl:		*/	snd_pcm_lib_ioctl,
/*	hw_params:	*/	snd_cs4281_hw_params,
/*	hw_free:	*/	snd_cs4281_hw_free,
/*	prepare:	*/	snd_cs4281_capture_prepare,
/*	trigger:	*/	snd_cs4281_trigger,
/*	pointer:	*/	snd_cs4281_pointer,
};
#else
static snd_pcm_ops_t snd_cs4281_playback_ops = {
	open:		snd_cs4281_playback_open,
	close:		snd_cs4281_playback_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_cs4281_hw_params,
	hw_free:	snd_cs4281_hw_free,
	prepare:	snd_cs4281_playback_prepare,
	trigger:	snd_cs4281_trigger,
	pointer:	snd_cs4281_pointer,
};

static snd_pcm_ops_t snd_cs4281_capture_ops = {
	open:		snd_cs4281_capture_open,
	close:		snd_cs4281_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_cs4281_hw_params,
	hw_free:	snd_cs4281_hw_free,
	prepare:	snd_cs4281_capture_prepare,
	trigger:	snd_cs4281_trigger,
	pointer:	snd_cs4281_pointer,
};
#endif

static void snd_cs4281_pcm_free(snd_pcm_t *pcm)
{
	cs4281_t *chip = snd_magic_cast(cs4281_t, pcm->private_data, return);
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __devinit snd_cs4281_pcm(cs4281_t * chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "CS4281", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cs4281_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_cs4281_capture_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_cs4281_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, "CS4281");
	chip->pcm = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024, 512*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  Mixer section
 */

static void snd_cs4281_mixer_free_ac97(ac97_t *ac97)
{
	cs4281_t *chip = snd_magic_cast(cs4281_t, ac97->private_data, return);
	if (ac97->num)
		chip->ac97_secondary = NULL;
	else
		chip->ac97 = NULL;
}

static int __devinit snd_cs4281_mixer(cs4281_t * chip)
{
	snd_card_t *card = chip->card;
	ac97_t ac97;
	int err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.write = snd_cs4281_ac97_write;
	ac97.read = snd_cs4281_ac97_read;
	ac97.private_data = chip;
	ac97.private_free = snd_cs4281_mixer_free_ac97;
	if ((err = snd_ac97_mixer(card, &ac97, &chip->ac97)) < 0)
		return err;
	if (chip->dual_codec) {
		ac97.num = 1;
		if ((err = snd_ac97_mixer(card, &ac97, &chip->ac97_secondary)) < 0)
			return err;
	}
#ifdef TARGET_OS2
	if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_cs4281_fmvol, chip))) < 0)
		return err;
#endif
	return 0;
}

/*

 */

static void snd_cs4281_proc_read(snd_info_entry_t *entry, 
				  snd_info_buffer_t * buffer)
{
	cs4281_t *chip = snd_magic_cast(cs4281_t, entry->private_data, return);

	snd_iprintf(buffer, "Cirrus Logic CS4281\n\n");
	snd_iprintf(buffer, "Spurious half IRQs   : %u\n", chip->spurious_dhtc_irq);
	snd_iprintf(buffer, "Spurious end IRQs    : %u\n", chip->spurious_dtc_irq);
}

static long snd_cs4281_BA0_read(snd_info_entry_t *entry, void *file_private_data,
				struct file *file, char *buf, long count)
{
	long size;
	cs4281_t *chip = snd_magic_cast(cs4281_t, entry->private_data, return -ENXIO);
	
	size = count;
	if (file->f_pos + size > CS4281_BA0_SIZE)
		size = (long)CS4281_BA0_SIZE - file->f_pos;
	if (size > 0) {
		char *tmp;
		long res;
		unsigned long virt;
		if ((tmp = kmalloc(size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		virt = chip->ba0 + file->f_pos;
		memcpy_fromio(tmp, virt, size);
		if (copy_to_user(buf, tmp, size))
			res = -EFAULT;
		else {
			res = size;
			file->f_pos += size;
		}
		kfree(tmp);
		return res;
	}
	return 0;
}

static long snd_cs4281_BA1_read(snd_info_entry_t *entry, void *file_private_data,
				struct file *file, char *buf, long count)
{
	long size;
	cs4281_t *chip = snd_magic_cast(cs4281_t, entry->private_data, return -ENXIO);
	
	size = count;
	if (file->f_pos + size > CS4281_BA1_SIZE)
		size = (long)CS4281_BA1_SIZE - file->f_pos;
	if (size > 0) {
		char *tmp;
		long res;
		unsigned long virt;
		if ((tmp = kmalloc(size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		virt = chip->ba1 + file->f_pos;
		memcpy_fromio(tmp, virt, size);
		if (copy_to_user(buf, tmp, size))
			res = -EFAULT;
		else {
			res = size;
			file->f_pos += size;
		}
		kfree(tmp);
		return res;
	}
	return 0;
}

#ifdef TARGET_OS2
static struct snd_info_entry_ops snd_cs4281_proc_ops_BA0 = {
	0, 0,
	snd_cs4281_BA0_read,
	0, 0, 0, 0, 0
};

static struct snd_info_entry_ops snd_cs4281_proc_ops_BA1 = {
	0, 0,
	snd_cs4281_BA1_read,
	0, 0, 0, 0, 0
};
#else
static struct snd_info_entry_ops snd_cs4281_proc_ops_BA0 = {
	read: snd_cs4281_BA0_read,
};

static struct snd_info_entry_ops snd_cs4281_proc_ops_BA1 = {
	read: snd_cs4281_BA1_read,
};

#endif

static void __devinit snd_cs4281_proc_init(cs4281_t * chip)
{
	snd_info_entry_t *entry;

	if ((entry = snd_info_create_card_entry(chip->card, "cs4281", chip->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = chip;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_cs4281_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	chip->proc_entry = entry;
	if ((entry = snd_info_create_card_entry(chip->card, "cs4281_BA0", chip->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = chip;
		entry->c.ops = &snd_cs4281_proc_ops_BA0;
		entry->size = CS4281_BA0_SIZE;
		if (snd_info_register(entry) < 0) {
			snd_info_unregister(entry);
			entry = NULL;
		}
	}
	chip->proc_entry_BA0 = entry;
	if ((entry = snd_info_create_card_entry(chip->card, "cs4281_BA1", chip->card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = chip;
		entry->c.ops = &snd_cs4281_proc_ops_BA1;
		entry->size = CS4281_BA1_SIZE;
		if (snd_info_register(entry) < 0) {
			snd_info_unregister(entry);
			entry = NULL;
		}
	}
	chip->proc_entry_BA1 = entry;
}

static void snd_cs4281_proc_done(cs4281_t * chip)
{
	if (chip->proc_entry_BA1) {
		snd_info_unregister(chip->proc_entry_BA1);
		chip->proc_entry_BA1 = NULL;
	}
	if (chip->proc_entry_BA0) {
		snd_info_unregister(chip->proc_entry_BA0);
		chip->proc_entry_BA0 = NULL;
	}
	if (chip->proc_entry) {
		snd_info_unregister(chip->proc_entry);
		chip->proc_entry = NULL;
	}
}

/*
 * joystick support
 */

#ifndef LINUX_2_2

typedef struct snd_cs4281_gameport {
	struct gameport info;
	cs4281_t *chip;
} cs4281_gameport_t;

static void snd_cs4281_gameport_trigger(struct gameport *gameport)
{
	cs4281_gameport_t *gp = (cs4281_gameport_t *)gameport;
	cs4281_t *chip;
	snd_assert(gp, return);
	chip = snd_magic_cast(cs4281_t, gp->chip, return);
	snd_cs4281_pokeBA0(chip, BA0_JSPT, 0xff);
}

static unsigned char snd_cs4281_gameport_read(struct gameport *gameport)
{
	cs4281_gameport_t *gp = (cs4281_gameport_t *)gameport;
	cs4281_t *chip;
	snd_assert(gp, return 0);
	chip = snd_magic_cast(cs4281_t, gp->chip, return 0);
	return snd_cs4281_peekBA0(chip, BA0_JSPT);
}

#ifdef COOKED_MODE
static int snd_cs4281_gameport_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	cs4281_gameport_t *gp = (cs4281_gameport_t *)gameport;
	cs4281_t *chip;
	unsigned js1, js2, jst;
	
	snd_assert(gp, return 0);
	chip = snd_magic_cast(cs4281_t, gp->chip, return 0);

	js1 = snd_cs4281_peekBA0(chip, BA0_JSC1);
	js2 = snd_cs4281_peekBA0(chip, BA0_JSC2);
	jst = snd_cs4281_peekBA0(chip, BA0_JSPT);
	
	*buttons = (~jst >> 4) & 0x0F; 
	
	axes[0] = ((js1 & JSC1_Y1V_MASK) >> JSC1_Y1V_SHIFT) & 0xFFFF;
	axes[1] = ((js1 & JSC1_X1V_MASK) >> JSC1_X1V_SHIFT) & 0xFFFF;
	axes[2] = ((js2 & JSC2_Y2V_MASK) >> JSC2_Y2V_SHIFT) & 0xFFFF;
	axes[3] = ((js2 & JSC2_X2V_MASK) >> JSC2_X2V_SHIFT) & 0xFFFF;

	for(jst=0;jst<4;++jst)
		if(axes[jst]==0xFFFF) axes[jst] = -1;
	return 0;
}
#endif

static int snd_cs4281_gameport_open(struct gameport *gameport, int mode)
{
	switch (mode) {
#ifdef COOKED_MODE
	case GAMEPORT_MODE_COOKED:
		return 0;
#endif
	case GAMEPORT_MODE_RAW:
		return 0;
	default:
		return -1;
	}
	return 0;
}

static void __devinit snd_cs4281_gameport(cs4281_t *chip)
{
	cs4281_gameport_t *gp;
	gp = kmalloc(sizeof(*gp), GFP_KERNEL);
	if (! gp) {
		snd_printk(KERN_ERR "cannot allocate gameport area\n");
		return;
	}
	memset(gp, 0, sizeof(*gp));
	gp->info.open = snd_cs4281_gameport_open;
	gp->info.read = snd_cs4281_gameport_read;
	gp->info.trigger = snd_cs4281_gameport_trigger;
#ifdef COOKED_MODE
	gp->info.cooked_read = snd_cs4281_gameport_cooked_read;
#endif
	gp->chip = chip;
	chip->gameport = gp;

	snd_cs4281_pokeBA0(chip, BA0_JSIO, 0xFF); // ?
	snd_cs4281_pokeBA0(chip, BA0_JSCTL, JSCTL_SP_MEDIUM_SLOW);
	gameport_register_port(&gp->info);
}

#endif /* !LINUX_2_2 */


/*

 */

static int snd_cs4281_free(cs4281_t *chip)
{
#ifndef LINUX_2_2
	if (chip->gameport) {
		gameport_unregister_port(&chip->gameport->info);
		kfree(chip->gameport);
	}
#endif
	snd_cs4281_proc_done(chip);
	synchronize_irq();

	/* Mask interrupts */
	snd_cs4281_pokeBA0(chip, BA0_HIMR, 0x7fffffff);
	/* Stop the DLL Clock logic. */
	snd_cs4281_pokeBA0(chip, BA0_CLKCR1, 0);
	/* Sound System Power Management - Turn Everything OFF */
	snd_cs4281_pokeBA0(chip, BA0_SSPM, 0);
	/* PCI interface - D3 state */
	pci_set_power_state(chip->pci, 3);

	if (chip->ba0)
		iounmap((void *) chip->ba0);
	if (chip->ba1)
		iounmap((void *) chip->ba1);
	if (chip->ba0_res) {
		release_resource(chip->ba0_res);
		kfree_nocheck(chip->ba0_res);
	}
	if (chip->ba1_res) {
		release_resource(chip->ba1_res);
		kfree_nocheck(chip->ba1_res);
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);

	snd_magic_kfree(chip);
	return 0;
}

static int snd_cs4281_dev_free(snd_device_t *device)
{
	cs4281_t *chip = snd_magic_cast(cs4281_t, device->device_data, return -ENXIO);
	return snd_cs4281_free(chip);
}

static int __devinit snd_cs4281_create(snd_card_t * card,
				    struct pci_dev *pci,
				    cs4281_t ** rchip,
				       int dual_codec)
{
	cs4281_t *chip;
	unsigned int tmp;
	signed long end_time;
	int err;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_cs4281_dev_free,0,0
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_cs4281_dev_free,
	};
#endif

	*rchip = NULL;
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	chip = snd_magic_kcalloc(cs4281_t, 0, GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	spin_lock_init(&chip->reg_lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->ba0_addr = pci_resource_start(pci, 0);
	chip->ba1_addr = pci_resource_start(pci, 1);
	pci_set_master(pci);
	if (dual_codec < 0 || dual_codec > 3) {
		snd_printk(KERN_ERR "invalid snd_dual_codec option %d\n", dual_codec);
		dual_codec = 0;
	}
	chip->dual_codec = dual_codec;

	if ((chip->ba0_res = request_mem_region(chip->ba0_addr, CS4281_BA0_SIZE, "CS4281 BA0")) == NULL) {
		snd_cs4281_free(chip);
		snd_printk(KERN_ERR "unable to grab memory region 0x%lx-0x%lx\n", chip->ba0_addr, chip->ba0_addr + CS4281_BA0_SIZE - 1);
		return -ENOMEM;
	}
	if ((chip->ba1_res = request_mem_region(chip->ba1_addr, CS4281_BA1_SIZE, "CS4281 BA1")) == NULL) {
		snd_cs4281_free(chip);
		snd_printk(KERN_ERR "unable to grab memory region 0x%lx-0x%lx\n", chip->ba1_addr, chip->ba1_addr + CS4281_BA1_SIZE - 1);
		return -ENOMEM;
	}
	if (request_irq(pci->irq, snd_cs4281_interrupt, SA_INTERRUPT|SA_SHIRQ, "CS4281", (void *)chip)) {
		snd_cs4281_free(chip);
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		return -ENOMEM;
	}
	chip->irq = pci->irq;

	chip->ba0 = (unsigned long) ioremap_nocache(chip->ba0_addr, CS4281_BA0_SIZE);
	chip->ba1 = (unsigned long) ioremap_nocache(chip->ba1_addr, CS4281_BA1_SIZE);
	if (!chip->ba0 || !chip->ba1) {
		snd_cs4281_free(chip);
		return -ENOMEM;
	}
	tmp = snd_cs4281_initcard(chip);
	if(tmp) {
		snd_cs4281_free(chip);
		return tmp;
	}

	snd_cs4281_proc_init(chip);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_cs4281_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;
}

static int snd_cs4281_initcard(cs4281_t *chip)
{
	unsigned int tmp;
	signed long end_time;
	int err;

	tmp = snd_cs4281_peekBA0(chip, BA0_CFLR);
	if (tmp != BA0_CFLR_DEFAULT) {
		snd_cs4281_pokeBA0(chip, BA0_CFLR, BA0_CFLR_DEFAULT);
		tmp = snd_cs4281_peekBA0(chip, BA0_CFLR);
		if (tmp != BA0_CFLR_DEFAULT) {
			snd_printk(KERN_ERR "CFLR setup failed (0x%x)\n", tmp);
			return -EIO;
		}
	}

	/* Set the 'Configuration Write Protect' register
	 * to 4281h.  Allows vendor-defined configuration
         * space between 0e4h and 0ffh to be written. */	
	snd_cs4281_pokeBA0(chip, BA0_CWPR, 0x4281);
	
	if ((tmp = snd_cs4281_peekBA0(chip, BA0_SERC1)) != (BA0_SERC1_SO1EN | BA0_SERC1_AC97)) {
		snd_printk(KERN_ERR "SERC1 AC'97 check failed (0x%x)\n", tmp);
		return -EIO;
	}
	if ((tmp = snd_cs4281_peekBA0(chip, BA0_SERC2)) != (BA0_SERC2_SI1EN | BA0_SERC2_AC97)) {
		snd_printk(KERN_ERR "SERC2 AC'97 check failed (0x%x)\n", tmp);
		return -EIO;
	}

	/* Sound System Power Management */
	snd_cs4281_pokeBA0(chip, BA0_SSPM, BA0_SSPM_MIXEN | BA0_SSPM_CSRCEN |
				           BA0_SSPM_PSRCEN | BA0_SSPM_JSEN |
				           BA0_SSPM_ACLEN | BA0_SSPM_FMEN);

	/* Serial Port Power Management */
 	/* Blast the clock control register to zero so that the
         * PLL starts out in a known state, and blast the master serial
         * port control register to zero so that the serial ports also
         * start out in a known state. */
	snd_cs4281_pokeBA0(chip, BA0_CLKCR1, 0);
	snd_cs4281_pokeBA0(chip, BA0_SERMC, 0);

        /* Make ESYN go to zero to turn off
         * the Sync pulse on the AC97 link. */
	snd_cs4281_pokeBA0(chip, BA0_ACCTL, 0);
	udelay(50);
                
	/*  Drive the ARST# pin low for a minimum of 1uS (as defined in the AC97
	 *  spec) and then drive it high.  This is done for non AC97 modes since
	 *  there might be logic external to the CS4281 that uses the ARST# line
	 *  for a reset. */
	snd_cs4281_pokeBA0(chip, BA0_SPMC, 0);
	udelay(50);
	snd_cs4281_pokeBA0(chip, BA0_SPMC, BA0_SPMC_RSTN);
	snd_cs4281_delay(50000);

	if (chip->dual_codec)
		snd_cs4281_pokeBA0(chip, BA0_SPMC, BA0_SPMC_RSTN | BA0_SPMC_ASDI2E);

	/*
	 *  Set the serial port timing configuration.
	 */
	snd_cs4281_pokeBA0(chip, BA0_SERMC,
			   (chip->dual_codec ? BA0_SERMC_TCID(chip->dual_codec) : BA0_SERMC_TCID(1)) |
			   BA0_SERMC_PTC_AC97 | BA0_SERMC_MSPE);

	/*
	 *  Start the DLL Clock logic.
	 */
	snd_cs4281_pokeBA0(chip, BA0_CLKCR1, BA0_CLKCR1_DLLP);
	snd_cs4281_delay(50000);
	snd_cs4281_pokeBA0(chip, BA0_CLKCR1, BA0_CLKCR1_SWCE | BA0_CLKCR1_DLLP);

	/*
	 * Wait for the DLL ready signal from the clock logic.
	 */
#ifdef TARGET_OS2
	end_time = (jiffies + 1000) + 1;
#else
	end_time = (jiffies + HZ / 4) + 1;
#endif
	do {
		/*
		 *  Read the AC97 status register to see if we've seen a CODEC
		 *  signal from the AC97 codec.
		 */
		if (snd_cs4281_peekBA0(chip, BA0_CLKCR1) & BA0_CLKCR1_DLLRDY)
			goto __ok0;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
        } while (end_time - (signed long)jiffies >= 0);

	snd_printk(KERN_ERR "DLLRDY not seen\n");
	return -EIO;

      __ok0:

	/*
	 *  The first thing we do here is to enable sync generation.  As soon
	 *  as we start receiving bit clock, we'll start producing the SYNC
	 *  signal.
	 */
	snd_cs4281_pokeBA0(chip, BA0_ACCTL, BA0_ACCTL_ESYN);

	/*
	 * Wait for the codec ready signal from the AC97 codec.
	 */
#ifdef TARGET_OS2
	end_time = (jiffies + 1000) + 1;
#else
	end_time = (jiffies + (3 * HZ) / 4) + 1;
#endif
	do {
		/*
		 *  Read the AC97 status register to see if we've seen a CODEC
		 *  signal from the AC97 codec.
		 */
		if (snd_cs4281_peekBA0(chip, BA0_ACSTS) & BA0_ACSTS_CRDY)
			goto __ok1;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
        } while (end_time - (signed long)jiffies >= 0);

	snd_printk(KERN_ERR "never read codec ready from AC'97 (0x%x)\n", snd_cs4281_peekBA0(chip, BA0_ACSTS));
	return -EIO;

      __ok1:
	if (chip->dual_codec) {
		end_time = (jiffies + (3 * HZ) / 4) + 1;
		do {
			if (snd_cs4281_peekBA0(chip, BA0_ACSTS2) & BA0_ACSTS_CRDY)
				goto __codec2_ok;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		} while (end_time - (signed long)jiffies >= 0);
		snd_printk(KERN_INFO "secondary codec doesn't respond. disable it...\n");
		chip->dual_codec = 0;
	__codec2_ok: ;
	}

	/*
	 *  Assert the valid frame signal so that we can start sending commands
	 *  to the AC97 codec.
	 */

	snd_cs4281_pokeBA0(chip, BA0_ACCTL, BA0_ACCTL_VFRM | BA0_ACCTL_ESYN);

	/*
	 *  Wait until we've sampled input slots 3 and 4 as valid, meaning that
	 *  the codec is pumping ADC data across the AC-link.
	 */

#ifdef TARGET_OS2
	end_time = (jiffies + 5000);
#else
	end_time = (jiffies + (5 * HZ) / 4) + 1;
#endif
	do {
		/*
		 *  Read the input slot valid register and see if input slots 3
		 *  4 are valid yet.
		 */
                if ((snd_cs4281_peekBA0(chip, BA0_ACISV) & (BA0_ACISV_SLV(3) | BA0_ACISV_SLV(4))) == (BA0_ACISV_SLV(3) | BA0_ACISV_SLV(4)))
                        goto __ok2;
                set_current_state(TASK_UNINTERRUPTIBLE);
                schedule_timeout(1);
        } while (end_time - (signed long)jiffies >= 0);

	snd_printk(KERN_ERR "never read ISV3 and ISV4 from AC'97\n");
	return -EIO;

      __ok2:

	/*
	 *  Now, assert valid frame and the slot 3 and 4 valid bits.  This will
	 *  commense the transfer of digital audio data to the AC97 codec.
	 */
	snd_cs4281_pokeBA0(chip, BA0_ACOSV, BA0_ACOSV_SLV(3) | BA0_ACOSV_SLV(4));

	/*
	 *  Initialize DMA structures
	 */
	for (tmp = 0; tmp < 4; tmp++) {
		cs4281_dma_t *dma = &chip->dma[tmp];
		dma->regDBA = BA0_DBA0 + (tmp * 0x10);
		dma->regDCA = BA0_DCA0 + (tmp * 0x10);
		dma->regDBC = BA0_DBC0 + (tmp * 0x10);
		dma->regDCC = BA0_DCC0 + (tmp * 0x10);
		dma->regDMR = BA0_DMR0 + (tmp * 8);
		dma->regDCR = BA0_DCR0 + (tmp * 8);
		dma->regHDSR = BA0_HDSR0 + (tmp * 4);
		dma->regFCR = BA0_FCR0 + (tmp * 4);
		dma->regFSIC = BA0_FSIC0 + (tmp * 4);
		dma->fifo_offset = tmp * CS4281_FIFO_SIZE;

		snd_cs4281_pokeBA0(chip, dma->regFCR,
				   BA0_FCR_LS(31) |
				   BA0_FCR_RS(31) |
				   BA0_FCR_SZ(CS4281_FIFO_SIZE) |
				   BA0_FCR_OF(dma->fifo_offset));
	}

	chip->src_left_play_slot = 0;	/* AC'97 left PCM playback (3) */
	chip->src_right_play_slot = 1;	/* AC'97 right PCM playback (4) */
	chip->src_left_rec_slot = 10;	/* AC'97 left PCM record (3) */
	chip->src_right_rec_slot = 11;	/* AC'97 right PCM record (4) */

#ifdef TARGET_OS2
        /* Activate wave playback FIFO for FM playback */
	chip->dma[0].valFCR = BA0_FCR_FEN | BA0_FCR_LS(0) |
		              BA0_FCR_RS(1) |
 	  	              BA0_FCR_SZ(CS4281_FIFO_SIZE) |
		              BA0_FCR_OF(chip->dma[0].fifo_offset);
	snd_cs4281_pokeBA0(chip, chip->dma[0].regFCR, chip->dma[0].valFCR);
	snd_cs4281_pokeBA0(chip, BA0_SRCSA, (chip->src_left_play_slot << 0) |
					    (chip->src_right_play_slot << 8) |
					    (chip->src_left_rec_slot << 16) |
					    (chip->src_right_rec_slot << 24));
#endif

	/* Initialize digital volume */
	snd_cs4281_pokeBA0(chip, BA0_PPLVC, 0);
	snd_cs4281_pokeBA0(chip, BA0_PPRVC, 0);

	/* Enable IRQs */
	snd_cs4281_pokeBA0(chip, BA0_HICR, BA0_HICR_EOI);
	/* Unmask interrupts */
	snd_cs4281_pokeBA0(chip, BA0_HIMR, 0x7fffffff & ~(
					BA0_HISR_MIDI |
					BA0_HISR_DMAI |
					BA0_HISR_DMA(0) |
					BA0_HISR_DMA(1) |
					BA0_HISR_DMA(2) |
					BA0_HISR_DMA(3)));
	synchronize_irq();
	return 0;
}

/*
 *  MIDI section
 */

static void snd_cs4281_midi_reset(cs4281_t *chip)
{
	snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr | BA0_MIDCR_MRST);
	udelay(100);
	snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
}

static int snd_cs4281_midi_input_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	cs4281_t *chip = snd_magic_cast(cs4281_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&chip->reg_lock, flags);
 	chip->midcr |= BA0_MIDCR_RXE;
	chip->midi_input = substream;
	if (!(chip->uartm & CS4281_MODE_OUTPUT)) {
		snd_cs4281_midi_reset(chip);
	} else {
		snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cs4281_midi_input_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	cs4281_t *chip = snd_magic_cast(cs4281_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->midcr &= ~(BA0_MIDCR_RXE | BA0_MIDCR_RIE);
	chip->midi_input = NULL;
	if (!(chip->uartm & CS4281_MODE_OUTPUT)) {
		snd_cs4281_midi_reset(chip);
	} else {
		snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	chip->uartm &= ~CS4281_MODE_INPUT;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cs4281_midi_output_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	cs4281_t *chip = snd_magic_cast(cs4281_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->uartm |= CS4281_MODE_OUTPUT;
	chip->midcr |= BA0_MIDCR_TXE;
	chip->midi_input = substream;
	if (!(chip->uartm & CS4281_MODE_INPUT)) {
		snd_cs4281_midi_reset(chip);
	} else {
		snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cs4281_midi_output_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	cs4281_t *chip = snd_magic_cast(cs4281_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->midcr &= ~(BA0_MIDCR_TXE | BA0_MIDCR_TIE);
	chip->midi_output = NULL;
	if (!(chip->uartm & CS4281_MODE_INPUT)) {
		snd_cs4281_midi_reset(chip);
	} else {
		snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	chip->uartm &= ~CS4281_MODE_OUTPUT;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static void snd_cs4281_midi_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	cs4281_t *chip = snd_magic_cast(cs4281_t, substream->rmidi->private_data, return);

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (up) {
		if ((chip->midcr & BA0_MIDCR_RIE) == 0) {
			chip->midcr |= BA0_MIDCR_RIE;
			snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	} else {
		if (chip->midcr & BA0_MIDCR_RIE) {
			chip->midcr &= ~BA0_MIDCR_RIE;
			snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_cs4281_midi_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	cs4281_t *chip = snd_magic_cast(cs4281_t, substream->rmidi->private_data, return);
	unsigned char byte;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (up) {
		if ((chip->midcr & BA0_MIDCR_TIE) == 0) {
			chip->midcr |= BA0_MIDCR_TIE;
			/* fill UART FIFO buffer at first, and turn Tx interrupts only if necessary */
			while ((chip->midcr & BA0_MIDCR_TIE) &&
			       (snd_cs4281_peekBA0(chip, BA0_MIDSR) & BA0_MIDSR_TBF) == 0) {
				if (snd_rawmidi_transmit(substream, &byte, 1) != 1) {
					chip->midcr &= ~BA0_MIDCR_TIE;
				} else {
					snd_cs4281_pokeBA0(chip, BA0_MIDWP, byte);
				}
			}
			snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	} else {
		if (chip->midcr & BA0_MIDCR_TIE) {
			chip->midcr &= ~BA0_MIDCR_TIE;
			snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

#ifdef TARGET_OS2
static snd_rawmidi_ops_t snd_cs4281_midi_output =
{
/*	open:		*/	snd_cs4281_midi_output_open,
/*	close:          */	snd_cs4281_midi_output_close,
/*	trigger:        */	snd_cs4281_midi_output_trigger,
};

static snd_rawmidi_ops_t snd_cs4281_midi_input =
{
/*	open:           */	snd_cs4281_midi_input_open,
/*	close:          */	snd_cs4281_midi_input_close,
/*	trigger:        */	snd_cs4281_midi_input_trigger,
};
#else
static snd_rawmidi_ops_t snd_cs4281_midi_output =
{
	open:           snd_cs4281_midi_output_open,
	close:          snd_cs4281_midi_output_close,
	trigger:        snd_cs4281_midi_output_trigger,
};

static snd_rawmidi_ops_t snd_cs4281_midi_input =
{
	open:           snd_cs4281_midi_input_open,
	close:          snd_cs4281_midi_input_close,
	trigger:        snd_cs4281_midi_input_trigger,
};

#endif

static int __devinit snd_cs4281_midi(cs4281_t * chip, int device, snd_rawmidi_t **rrawmidi)
{
	snd_rawmidi_t *rmidi;
	int err;

	if (rrawmidi)
		*rrawmidi = NULL;
	if ((err = snd_rawmidi_new(chip->card, "CS4281", device, 1, 1, &rmidi)) < 0)
		return err;
	strcpy(rmidi->name, "CS4281");
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_cs4281_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_cs4281_midi_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = chip;
	chip->rmidi = rmidi;
	if (rrawmidi)
		*rrawmidi = rmidi;
	return 0;
}

/*
 *  Interrupt handler
 */

static void snd_cs4281_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	cs4281_t *chip = snd_magic_cast(cs4281_t, dev_id, return);
	unsigned int status, dma, val;
	cs4281_dma_t *cdma;
#ifdef TARGET_OS2
	int fOurIrq = FALSE;
#endif

	if (chip == NULL)
		return;
	status = snd_cs4281_peekBA0(chip, BA0_HISR);
	if ((status & 0x7fffffff) == 0) {
		snd_cs4281_pokeBA0(chip, BA0_HICR, BA0_HICR_EOI);
		return;
	}

	if (status & (BA0_HISR_DMA(0)|BA0_HISR_DMA(1)|BA0_HISR_DMA(2)|BA0_HISR_DMA(3))) {
#ifdef TARGET_OS2
		fOurIrq = TRUE;
#endif
		for (dma = 0; dma < 4; dma++)
			if (status & BA0_HISR_DMA(dma)) {
				cdma = &chip->dma[dma];
				spin_lock(&chip->reg_lock);
				/* ack DMA IRQ */
				val = snd_cs4281_peekBA0(chip, cdma->regHDSR);
				/* workaround, sometimes CS4281 acknowledges */
				/* end or middle transfer position twice */
				cdma->frag++;
				if ((val & BA0_HDSR_DHTC) && !(cdma->frag & 1)) {
					cdma->frag--;
					chip->spurious_dhtc_irq++;
					spin_unlock(&chip->reg_lock);
					continue;
				}
				if ((val & BA0_HDSR_DTC) && (cdma->frag & 1)) {
					cdma->frag--;
					chip->spurious_dtc_irq++;
					spin_unlock(&chip->reg_lock);
					continue;
				}
				spin_unlock(&chip->reg_lock);
				snd_pcm_period_elapsed(cdma->substream);
			}
	}

	if ((status & BA0_HISR_MIDI) && chip->rmidi) {
		unsigned char c;

#ifdef TARGET_OS2
		fOurIrq = TRUE;
#endif
		
		spin_lock(&chip->reg_lock);
		while ((snd_cs4281_peekBA0(chip, BA0_MIDSR) & BA0_MIDSR_RBE) == 0) {
			c = snd_cs4281_peekBA0(chip, BA0_MIDRP);
			if ((chip->midcr & BA0_MIDCR_RIE) == 0)
				continue;
			snd_rawmidi_receive(chip->midi_input, &c, 1);
		}
		while ((snd_cs4281_peekBA0(chip, BA0_MIDSR) & BA0_MIDSR_TBF) == 0) {
			if ((chip->midcr & BA0_MIDCR_TIE) == 0)
				break;
			if (snd_rawmidi_transmit(chip->midi_output, &c, 1) != 1) {
				chip->midcr &= ~BA0_MIDCR_TIE;
				snd_cs4281_pokeBA0(chip, BA0_MIDCR, chip->midcr);
				break;
			}
			snd_cs4281_pokeBA0(chip, BA0_MIDWP, c);
		}
		spin_unlock(&chip->reg_lock);
	}

	/* EOI to the PCI part... reenables interrupts */
	snd_cs4281_pokeBA0(chip, BA0_HICR, BA0_HICR_EOI);

#ifdef TARGET_OS2
	if (fOurIrq) {
		eoi_irq(irq);
	}
#endif
}


static int __devinit snd_cs4281_probe(struct pci_dev *pci,
				      const struct pci_device_id *id)
{
	static int dev;
	snd_card_t *card;
	cs4281_t *chip;
	opl3_t *opl3;
	int err;

        if (dev >= SNDRV_CARDS)
                return -ENODEV;
	if (!snd_enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	if ((err = snd_cs4281_create(card, pci, &chip, snd_dual_codec[dev])) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_cs4281_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4281_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs4281_midi(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_opl3_create(card,
				   (chip->ba0 + BA0_B0AP) >> 2,
				   (chip->ba0 + BA0_B1AP) >> 2,
				   OPL3_HW_OPL3_CS4281, 1, &opl3)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
#ifndef LINUX_2_2
	snd_cs4281_gameport(chip);
#endif
	strcpy(card->driver, "CS4281");
	strcpy(card->shortname, "Cirrus Logic CS4281");
	sprintf(card->longname, "%s at 0x%lx, irq %d",
		card->shortname,
		chip->ba0_addr,
		chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

#ifdef TARGET_OS2
	pci_set_drvdata(pci, chip);
#else
	pci_set_drvdata(pci, card);
#endif
	dev++;
	return 0;
}

static void __devexit snd_cs4281_remove(struct pci_dev *pci)
{
#ifdef TARGET_OS2
	cs4281_t *chip = pci_get_drvdata(pci);
	snd_card_free(chip->card);
#else
	snd_card_free(pci_get_drvdata(pci));
#endif
	pci_set_drvdata(pci, NULL);
}

#ifdef TARGET_OS2
#define CLKCR1_CKRA                             0x00010000L

static void snd_cs4281_suspend(struct pci_dev *dev, u32 state)
{
    cs4281_t *chip = pci_get_drvdata(dev);
    u32 ulCodec;
    u32 ulCLK;
    u32 ulSSCR;

    ulCLK = snd_cs4281_peekBA0(chip, BA0_CLKCR1);
    ulCLK |= CLKCR1_CKRA;
    snd_cs4281_pokeBA0(chip, BA0_CLKCR1, ulCLK);

    //
    // Disable interrupts.
    //
    snd_cs4281_pokeBA0(chip, BA0_HICR, BA0_HICR_CHGM);

    ulSSCR = snd_cs4281_peekBA0(chip, BA0_SSCR);

    //
    // Turn off the serial ports.
    //
    snd_cs4281_pokeBA0(chip, BA0_SERMC, 0);

    //
    // Power off FM, Joystick, AC link,
    //
    snd_cs4281_pokeBA0(chip, BA0_SSPM, 0);

    //
    // DLL off.
    //
    snd_cs4281_pokeBA0(chip, BA0_CLKCR1, 0);

    //
    // AC link off.
    //
    snd_cs4281_pokeBA0(chip, BA0_SPMC, 0);

    ulCLK = snd_cs4281_peekBA0(chip, BA0_CLKCR1);
    ulCLK &= ~CLKCR1_CKRA;
    snd_cs4281_pokeBA0(chip, BA0_CLKCR1, ulCLK);

    return;
}

static void snd_cs4281_resume(struct pci_dev *dev)
{
    cs4281_t *chip = pci_get_drvdata(dev);
    u32 ulCodec;
    u32 ulCLK;
    u32 ulSSCR;

    ulCLK = snd_cs4281_peekBA0(chip, BA0_CLKCR1);
    ulCLK |= CLKCR1_CKRA;
    snd_cs4281_pokeBA0(chip, BA0_CLKCR1, ulCLK);

    snd_cs4281_initcard(chip);

    ulCLK = snd_cs4281_peekBA0(chip, BA0_CLKCR1);
    ulCLK &= ~CLKCR1_CKRA;
    snd_cs4281_pokeBA0(chip, BA0_CLKCR1, ulCLK);

    return;
}

static struct pci_driver driver = {
        0, 0,
	"CS4281",
	snd_cs4281_ids,
	snd_cs4281_probe,
	snd_cs4281_remove,
	snd_cs4281_suspend,
	snd_cs4281_resume,
};
#else
static struct pci_driver driver = {
	name: "CS4281",
	id_table: snd_cs4281_ids,
	probe: snd_cs4281_probe,
	remove: __devexit_p(snd_cs4281_remove),
};
#endif
	
static int __init alsa_card_cs4281_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "CS4281 soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_cs4281_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_cs4281_init)
module_exit(alsa_card_cs4281_exit)

#ifndef MODULE

/* format is: snd-cs4281=snd_enable,snd_index,snd_id */

static int __init alsa_card_cs4281_setup(char *str)
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

__setup("snd-cs4281=", alsa_card_cs4281_setup);

#endif /* ifndef MODULE */
