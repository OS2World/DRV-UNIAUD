#ifndef __HAL2_H
#define __HAL2_H

/*
 *  Global structures used for HAL2 part of ALSA driver
 *  Copyright (c) by Ulf Carlsson <ulfc@bun.falkenberg.se>
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

#include "pcm.h"
#include "rawmidi.h"
#include "timer.h"

#include <asm/addrspace.h>
#include <asm/sgi/sgihpc.h>

/* We can possibly use the addresses in hpc_regs instead of giving the adress in
 * an absolute way. This will at least save us if someone breaks the hpc_regs
 * structure again :-)
 */

#define H2_HAL2_BASE		(HPC3_CHIP0_PBASE + 0x58000)
#define H2_CTL_PIO		(H2_HAL2_BASE + 0 * 0x400)
#define H2_AES_PIO		(H2_HAL2_BASE + 1 * 0x400)
#define H2_VOL_PIO		(H2_HAL2_BASE + 2 * 0x400)
#define H2_SYN_PIO		(H2_HAL2_BASE + 3 * 0x400)

/* Indirect status register */

#define H2_ISR_TSTATUS		0x01	/* RO: transaction status 1=busy */
#define H2_ISR_USTATUS		0x02	/* RO: utime status bit 1=armed */
#define H2_ISR_QUAD_MODE	0x04	/* codec mode 0=indigo 1=quad */
#define H2_ISR_GLOBAL_RESET_N	0x08	/* chip global reset 0=reset */
#define H2_ISR_CODEC_RESET_N	0x10	/* codec/synth reset 0=reset  */

	/* Revision register */

#define H2_REV_AUDIO_PRESENT	0x8000	/* RO: audio present 0=present */
#define H2_REV_BOARD_M		0x7000	/* RO: bits 14:12, board revision */
#define H2_REV_MAJOR_CHIP_M	0x00F0	/* RO: bits 7:4, major chip revision */
#define H2_REV_MINOR_CHIP_M	0x000F	/* RO: bits 3:0, minor chip revision */

/* Indirect address register */

/*
 * Address of indirect internal register to be accessed. A write to this
 * register initiates read or write access to the indirect registers in the
 * HAL2. Note that there af four indirect data registers for write access to
 * registers larger than 16 byte.
 */

#define H2_IAR_TYPE_M		0xF000	/* bits 15:12, type of functional */
					/* block the register resides in */
					/* 1=DMA Port */
					/* 9=Global DMA Control */
					/* 2=Bresenham */
					/* 3=Unix Timer */
#define H2_IAR_NUM_M		0x0F00	/* bits 11:8 instance of the */
					/* blockin which the indirect */
					/* register resides */
					/* If IAR_TYPE_M=DMA Port: */
					/* 1=Synth In */
					/* 2=AES In */
					/* 3=AES Out */
					/* 4=DAC Out */
					/* 5=ADC Out */
					/* 6=Synth Control */
					/* If IAR_TYPE_M=Global DMA Control: */
					/* 1=Control */
					/* If IAR_TYPE_M=Bresenham: */
					/* 1=Bresenham Clock Gen 1 */
					/* 2=Bresenham Clock Gen 2 */
					/* 3=Bresenham Clock Gen 3 */
					/* If IAR_TYPE_M=Unix Timer: */
					/* 1=Unix Timer */
#define H2_IAR_ACCESS_SELECT	0x0080	/* 1=read 0=write */
#define H2_IAR_PARAM		0x000C	/* Parameter Select */
#define H2_IAR_RB_INDEX_M	0x0003	/* Read Back Index */
					/* 00:word0 */
					/* 01:word1 */
					/* 10:word2 */
					/* 11:word3 */
/*
 * HAL2 internal addressing
 *
 * The HAL2 has "indirect registers" (idr) which are accessed by writing to the
 * Indirect Data registers. Write the address to the Indirect Address register
 * to transfer the data.
 *
 * We define the H2IR_* to the read address and H2IW_* to the write address and
 * H2I_* to be fields in whatever register is referred to.
 *
 * When we write to indirect registers which are larger than one word (16 bit)
 * we have to fill more than one indirect register before writing. When we read
 * back however we have to read several times, each time with different Read
 * Back Indexes (there are defs for doing this easily).
 */

/*
 * Relay Control
 */
#define H2I_RELAY_C		0x9100
#define H2I_RELAY_C_STATE	0x01		/* state of RELAY pin signal */

/* DMA port enable */

#define H2I_DMA_PORT_EN		0x9104
#define H2I_DMA_PORT_EN_SY_IN	0x01		/* Synth_in DMA port */
#define H2I_DMA_PORT_EN_AESRX	0x02		/* AES receiver DMA port */
#define H2I_DMA_PORT_EN_AESTX	0x04		/* AES transmitter DMA port */
#define H2I_DMA_PORT_EN_CODECTX	0x08		/* CODEC transmit DMA port */
#define H2I_DMA_PORT_EN_CODECR	0x10		/* CODEC receive DMA port */

#define H2I_DMA_END		0x9108 		/* global dma endian select */
#define H2I_DMA_END_SY_IN	0x01		/* Synth_in DMA port */
#define H2I_DMA_END_AESRX	0x02		/* AES receiver DMA port */
#define H2I_DMA_END_AESTX	0x04		/* AES transmitter DMA port */
#define H2I_DMA_END_CODECTX	0x08		/* CODEC transmit DMA port */
#define H2I_DMA_END_CODECR	0x10		/* CODEC receive DMA port */
						/* 0=b_end 1=l_end */

#define H2I_DMA_DRV		0x910C  	/* global PBUS DMA enable */

#define H2I_SYNTH_C		0x1104		/* Synth DMA control */

#define H2I_AESRX_C		0x1204	 	/* AES RX dma control */
#define H2I_AESRX_C_TS_EN	0x20		/* timestamp enable */
#define H2I_AESRX_C_TS_FMT	0x40		/* timestamp format */
#define H2I_AESRX_C_NAUDIO	0x80		/* PBUS DMA data format */

/* AESRX CTL, 16 bit */

#define H2I_AESTX_C		0x1304		/* AES TX DMA control */
#define H2I_AESTX_C_CLKID_SHIFT	3		/* Bresenham Clock Gen 1-3 */
#define H2I_AESTX_C_CLKID_M	0x18
#define H2I_AESTX_C_DATAT_SHIFT	8		/* 1=mono 2=stereo (3=quad) */
#define H2I_AESTX_C_DATAT_M	0x300

/* DAC CTL1, 16 bit */

#define H2I_DAC_C1		0x1404 		/* DAC dma control */
#define H2I_DAC_C1_DMA_SHIFT	0		/* DMA channel */
#define H2I_DAC_C1_DMA_M	0x7
#define H2I_DAC_C1_CLKID_SHIFT	3		/* Bresenham Clock Gen 1-3 */
#define H2I_DAC_C1_CLKID_M	0x18
#define H2I_DAC_C1_DATAT_SHIFT	8		/* 1=mono 2=stereo (3=quad) */
#define H2I_DAC_C1_DATAT_M	0x300

/* DAC CTL2, 32 bit */

#define H2I_DAC_C2		0x1408
#define H2I_DAC_C2_R_GAIN_SHIFT	0		/* right a/d input gain */	
#define H2I_DAC_C2_R_GAIN_M	0xf	
#define H2I_DAC_C2_L_GAIN_SHIFT	4		/* left a/d input gain */
#define H2I_DAC_C2_L_GAIN_M	0xf0
#define H2I_DAC_C2_R_SEL	0x100		/* right input select */
#define H2I_DAC_C2_L_SEL	0x200		/* left input select */
#define H2I_DAC_C2_MUTE		0x400		/* mute */
#define H2I_DAC_C2_DO1		0x10000		/* digital output port bit 0 */
#define H2I_DAC_C2_DO2		0x20000		/* digital output port bit 1 */
#define H2I_DAC_C2_R_ATT_SHIFT	18		/* right a/d output - */
#define H2I_DAC_C2_R_ATT_M	0x7c0000	/* attenuation */
#define H2I_DAC_C2_L_ATT_SHIFT	23		/* left a/d output - */
#define H2I_DAC_C2_L_ATT_M	0x0000f80	/* attenuation */

/* ADC CTL1, 16 bit */

#define H2I_ADC_C1		0x1504 		/* DAC dma control */
#define H2I_ADC_C1_DMA_SHIFT	0		/* DMA channel */
#define H2I_ADC_C1_DMA_M	0x7
#define H2I_ADC_C1_CLKID_SHIFT	3		/* Bresenham Clock Gen 1-3 */
#define H2I_ADC_C1_CLKID_M	0x18
#define H2I_ADC_C1_DATAT_SHIFT	8		/* 1=mono 2=stereo (3=quad) */
#define H2I_ADC_C1_DATAT_M	0x300

/* ADC CTL2, 32 bit */

#define H2I_ADC_C2		0x1508
#define H2I_ADC_C2_RGAIN_SHIFT	0		/* right a/d input gain */	
#define H2I_ADC_C2_R_GAIN_M	0xf	
#define H2I_ADC_C2_L_GAIN_SHIFT	4		/* left a/d input gain */
#define H2I_ADC_C2_L_GAIN_M	0xf0
#define H2I_ADC_C2_R_SEL	0x100		/* right input select */
#define H2I_ADC_C2_L_SEL	0x200		/* left input select */
#define H2I_ADC_C2_MUTE		0x400		/* mute */
#define H2I_ADC_C2_DO1		0x10000		/* digital output port bit 0 */
#define H2I_ADC_C2_DO2		0x20000		/* digital output port bit 1 */
#define H2I_ADC_C2_R_ATT_SHIFT	18		/* right a/d output - */
#define H2I_ADC_C2_R_ATT_M	0x7c0000	/* attenuation */
#define H2I_ADC_C2_L_ATT_SHIFT	23		/* left a/d output - */
#define H2I_ADC_C2_L_ATT_M	0x0000f80	/* attenuation */


#define H2I_SYNTH_MAP_C		0x1104		/* synth dma handshake ctrl */

/* Clock generator 1 CTL 1, 16 bit */

#define H2I_BRES1_C1		0x2104 
#define H2I_BRES1_C1_SHIFT	0		/* 0=48.0 1=44.1 2=aes_rx */
#define H2I_BRES1_C1_M		0x03
				
/* Clock generator 1 CTL 2, 32 bit */

#define H2I_BRES1_C2		0x2108
#define H2I_BRES1_C2_INC_SHIFT	0		/* increment value */
#define H2I_BRES1_C2_INC_M	0xffff
#define H2I_BRES1_C2_MOD_SHIFT	16		/* modcontrol value */
#define H2I_BRES1_C2_MOD_M	0xffff0000	/* modctrl=0xffff&(modinc-1) */

/* Clock generator 2 CTL 1, 16 bit */

#define H2I_BRES2_C1		0x2204 
#define H2I_BRES2_C1_SHIFT	0		/* 0=48.0 1=44.1 2=aes_rx */
#define H2I_BRES2_C1_M		0x03
				
/* Clock generator 2 CTL 2, 32 bit */

#define H2I_BRES2_C2		0x2208
#define H2I_BRES2_C2_INC_SHIFT	0		/* increment value */
#define H2I_BRES2_C2_INC_M	0xffff
#define H2I_BRES2_C2_MOD_SHIFT	16		/* modcontrol value */
#define H2I_BRES2_C2_MOD_M	0xffff0000	/* modctrl=0xffff&(modinc-1) */

/* Clock generator 3 CTL 1, 16 bit */

#define H2I_BRES3_C1		0x2304 
#define H2I_BRES3_C1_SHIFT	0		/* 0=48.0 1=44.1 2=aes_rx */
#define H2I_BRES3_C1_M		0x03
				
/* Clock generator 3 CTL 2, 32 bit */

#define H2I_BRES3_C2		0x2308
#define H2I_BRES3_C2_INC_SHIFT	0		/* increment value */
#define H2I_BRES3_C2_INC_M	0xffff
#define H2I_BRES3_C2_MOD_SHIFT	16		/* modcontrol value */
#define H2I_BRES3_C2_MOD_M	0xffff0000	/* modctrl=0xffff&(modinc-1) */

/* Unix timer, 64 bit */

#define H2I_UTIME		0x3104
#define H2I_UTIME_0_LD		0xffff		/* microseconds, LSB's */
#define H2I_UTIME_1_LD0		0x0f		/* microseconds, MSB's */
#define H2I_UTIME_1_LD1		0xf0		/* tenths of microseconds */
#define H2I_UTIME_2_LD		0xffff		/* seconds, LSB's */
#define H2I_UTIME_3_LD		0xffff		/* seconds, MSB's */

typedef volatile unsigned long snd_hal2_reg_t;

typedef struct _snd_hal2_ctl_regs snd_hal2_ctl_regs_t;

struct _snd_hal2_ctl_regs {
	snd_hal2_reg_t _unused0[4];
	snd_hal2_reg_t isr;		/* 0x10 Status Register */
	snd_hal2_reg_t _unused1[3];
	snd_hal2_reg_t rev;		/* 0x20 Revision Register */
	snd_hal2_reg_t _unused2[3];
	snd_hal2_reg_t iar;		/* 0x30 Indirect Address Register */
	snd_hal2_reg_t _unused3[3];
	snd_hal2_reg_t idr0;		/* 0x40 Indirect Data Register 0 */
	snd_hal2_reg_t _unused4[3];
	snd_hal2_reg_t idr1;		/* 0x50 Indirect Data Register 1 */
	snd_hal2_reg_t _unused5[3];
	snd_hal2_reg_t idr2;		/* 0x60 Indirect Data Register 2 */
	snd_hal2_reg_t _unused6[3];
	snd_hal2_reg_t idr3;		/* 0x70 Indirect Data Register 3 */
};

typedef struct _snd_hal2_aes_regs snd_hal2_aes_regs_t;

struct _snd_hal2_aes_regs {
	snd_hal2_reg_t rx_stat[2];	/* Status registers */
	snd_hal2_reg_t rx_cr[2];	/* Control registers */
	snd_hal2_reg_t rx_ud[4];	/* User data window */
	snd_hal2_reg_t rx_st[24];	/* Channel status data */
	
	snd_hal2_reg_t tx_stat[1];	/* Status register */
	snd_hal2_reg_t tx_cr[3];	/* Control registers */
	snd_hal2_reg_t tx_ud[4];	/* User data window */
	snd_hal2_reg_t tx_st[24];	/* Channel status data */
};

typedef struct _snd_hal2_vol_regs snd_hal2_vol_regs_t;

struct _snd_hal2_vol_regs {
	snd_hal2_reg_t right;		/* 0x00 Right volume */
	snd_hal2_reg_t left;		/* 0x04 Left volume */
};

typedef struct _snd_hal2_syn_regs snd_hal2_syn_regs_t;

struct _snd_hal2_syn_regs {
	snd_hal2_reg_t _unused0[2];
	snd_hal2_reg_t page;		/* DOC Page register */
	snd_hal2_reg_t regsel;		/* DOC Register selection */
	snd_hal2_reg_t dlow;		/* DOC Data low */
	snd_hal2_reg_t dhigh;		/* DOC Data high */
	snd_hal2_reg_t irq;		/* IRQ Status */
	snd_hal2_reg_t dram;		/* DRAM Access */
};

struct _snd_hal2_card;
typedef struct _snd_hal2_card snd_hal2_card_t;

/* HAL2 specific structure */

typedef struct _snd_hal2_ring snd_hal2_ring_t;

struct _snd_hal2_ring {
	volatile struct hpc_dma_desc desc;
	unsigned long _padding;		/* 8 byte aligned */
};

typedef struct _snd_hal2_pbus snd_hal2_pbus_t;

struct _snd_hal2_pbus {
	struct hpc3_pbus_dmacregs *pbus;
	int pbusnr;
	unsigned long fifobeg;
	unsigned long fifoend;
	unsigned long highwater;

	unsigned long ctrl;		/* Current state of pbus->pbdma_ctrl */
};

typedef struct _snd_hal2_codec snd_hal2_codec_t;

struct _snd_hal2_codec {
	snd_dma_t *dmaptr;		/* DMA1 pointer */
	snd_hal2_ring_t *ringbuf;	/* DMA descirptors */
	snd_hal2_pbus_t pbus;
	unsigned short blocks;
};

struct _snd_hal2 {
	snd_irq_t *irqptr;		/* IRQ pointer */
	snd_hal2_ctl_regs_t *ctl_regs;	/* HAL2 ctl registers */
	snd_hal2_aes_regs_t *aes_regs;	/* HAL2 vol registers */
	snd_hal2_vol_regs_t *vol_regs;	/* HAL2 aes registers */
	snd_hal2_syn_regs_t *syn_regs;	/* HAL2 syn registers */

	unsigned int master;		/* Master frequency */
	unsigned short mod;		/* MOD value */
	unsigned short inc;		/* INC value */

	snd_hal2_codec_t dac;
	snd_hal2_codec_t adc;
};

/* main structure for HAL2 card */

struct _snd_hal2_card {
	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_rawmidi_t *midi_uart;

	snd_pcm_substream_t *playback_substream;
	snd_pcm_substream_t *capture_substream;
	snd_pcm1_substream_t *playback_substream1;
	snd_pcm1_substream_t *capture_substream1;

	struct _snd_hal2 hal2;	/* HAL2 specific variables */

	int usecount;
};

extern void snd_hal2_isr_write(snd_hal2_card_t *hal2, unsigned short val);
extern unsigned short snd_hal2_isr_look(snd_hal2_card_t *hal2);
extern unsigned short snd_hal2_rev_look(snd_hal2_card_t *hal2);

extern void snd_hal2_i_write16(snd_hal2_card_t *hal2, unsigned short addr, unsigned short val);
extern void snd_hal2_i_write32(snd_hal2_card_t *hal2, unsigned short addr, unsigned long val);

extern unsigned short snd_hal2_i_look16(snd_hal2_card_t *hal2, unsigned short addr);
extern unsigned long snd_hal2_i_look32(snd_hal2_card_t *hal2, unsigned short addr);

extern snd_pcm_t *snd_hal2_pcm_new_device(snd_hal2_card_t *hal2);
extern void snd_hal2_dump_regs(snd_hal2_card_t *hal2);
extern snd_pcm_t *snd_hal2_new_device(snd_card_t *card,
				      snd_irq_t *irqnum,
				      snd_dma_t *dma1ptr,
				      snd_dma_t *dma2ptr,
				      snd_hal2_ctl_regs_t *ctl_regs,
				      snd_hal2_aes_regs_t *aes_regs,
				      snd_hal2_vol_regs_t *vol_regs,
				      snd_hal2_syn_regs_t *syn_regs);
extern void snd_hal2_interrupt(snd_hal2_card_t * hal2);

extern snd_pcm_t *snd_hal2_pcm(snd_hal2_card_t *hal2);

#endif				/* __HAL2_H */
