#ifndef __MPU401_H
#define __MPU401_H

/*
 *  Header file for MPU-401 and compatible cards
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

#include "rawmidi.h"

#define MPU401_HW_MPU401		1	/* native MPU401 */
#define MPU401_HW_SB			2	/* SoundBlaster MPU-401 UART */
#define MPU401_HW_ES1688		3	/* AudioDrive ES1688 MPU-401 UART */
#define MPU401_HW_OPL3SA2		4	/* Yamaha OPL3-SA2 */
#define MPU401_HW_SONICVIBES		5	/* S3 SonicVibes */
#define MPU401_HW_CS4232		6	/* CS4232 */
#define MPU401_HW_ES18XX		7	/* AudioDrive ES18XX MPU-401 UART */
#define MPU401_HW_FM801			8	/* ForteMedia FM801 */
#define MPU401_HW_TRID4DWAVE		9	/* Trident 4DWave */
#define MPU401_HW_AZT2320		10	/* Aztech AZT2320 */
#define MPU401_HW_ALS100		11	/* Avance Logic ALS100 */
#define MPU401_HW_ICE1712		12	/* Envy24 */
#define MPU401_HW_VIA686A		13	/* VIA 82C686A */
#define MPU401_HW_YMFPCI		14	/* YMF DS-XG PCI */
#define MPU401_HW_CMIPCI		15	/* CMIPCI MPU-401 UART */
#define MPU401_HW_ALS4000		16	/* Avance Logic ALS4000 */

#define MPU401_MODE_BIT_INPUT		0
#define MPU401_MODE_BIT_OUTPUT		1
#define MPU401_MODE_BIT_INPUT_TRIGGER	2
#define MPU401_MODE_BIT_OUTPUT_TRIGGER	3
#define MPU401_MODE_BIT_RX_LOOP		4
#define MPU401_MODE_BIT_TX_LOOP		5

#define MPU401_MODE_INPUT		(1<<MPU401_MODE_BIT_INPUT)
#define MPU401_MODE_OUTPUT		(1<<MPU401_MODE_BIT_OUTPUT)
#define MPU401_MODE_INPUT_TRIGGER	(1<<MPU401_MODE_BIT_INPUT_TRIGGER)
#define MPU401_MODE_OUTPUT_TRIGGER	(1<<MPU401_MODE_BIT_OUTPUT_TRIGGER)

#define MPU401_MODE_INPUT_TIMER		(1<<0)
#define MPU401_MODE_OUTPUT_TIMER	(1<<1)

typedef struct _snd_mpu401 mpu401_t;

struct _snd_mpu401 {
	snd_rawmidi_t *rmidi;

	unsigned short hardware;	/* MPU401_HW_XXXX */
	unsigned long port;		/* base port of MPU-401 chip */
	struct resource *res;		/* port resource */
	int irq;			/* IRQ number of MPU-401 chip (-1 = poll) */
	int irq_flags;

	unsigned int mode;		/* MPU401_MODE_XXXX */
	int timer_invoked;

	void (*open_input) (mpu401_t * mpu);
	void (*close_input) (mpu401_t * mpu);
	void (*open_output) (mpu401_t * mpu);
	void (*close_output) (mpu401_t * mpu);
	void *private_data;

	snd_rawmidi_substream_t *substream_input;
	snd_rawmidi_substream_t *substream_output;

	spinlock_t open_lock;
	spinlock_t input_lock;
	spinlock_t output_lock;
	spinlock_t timer_lock;

	struct timer_list timer;
};

/* I/O ports */

#define MPU401C(mpu) ((mpu)->port + 1)
#define MPU401D(mpu) ((mpu)->port + 0)

/*

 */

void snd_mpu401_uart_interrupt(int irq, void *dev_id, struct pt_regs *regs);

int snd_mpu401_uart_new(snd_card_t * card,
			int device,
			unsigned short hardware,
			unsigned long port,
			int integrated,
			int irq,
			int irq_flags,
			snd_rawmidi_t ** rrawmidi);

#endif				/* __MPU401_H */
