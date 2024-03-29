#ifndef __MINORS_H
#define __MINORS_H

/*
 *  MINOR numbers
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

#define SNDRV_MINOR_DEVICES		32
#define SNDRV_MINOR_CARD(minor)		((minor) >> 5)
#define SNDRV_MINOR_DEVICE(minor)	((minor) & 0x001f)
#define SNDRV_MINOR(card, dev)		(((card) << 5) | (dev))

#define SNDRV_MINOR_CONTROL		0	/* 0 - 0 */
#define SNDRV_MINOR_SEQUENCER		1
#define SNDRV_MINOR_TIMER		(1+32)
#define SNDRV_MINOR_HWDEP		4	/* 4 - 7 */
#define SNDRV_MINOR_HWDEPS		4
#define SNDRV_MINOR_RAWMIDI		8	/* 8 - 11 */
#define SNDRV_MINOR_RAWMIDIS		4
#define SNDRV_MINOR_PCM_PLAYBACK	16	/* 16 - 23 */
#define SNDRV_MINOR_PCM_CAPTURE		24	/* 24 - 31 */
#define SNDRV_MINOR_PCMS		8

#define SNDRV_DEVICE_TYPE_CONTROL	SNDRV_MINOR_CONTROL
#define SNDRV_DEVICE_TYPE_HWDEP		SNDRV_MINOR_HWDEP
#define SNDRV_DEVICE_TYPE_MIXER		SNDRV_MINOR_MIXER
#define SNDRV_DEVICE_TYPE_RAWMIDI	SNDRV_MINOR_RAWMIDI
#define SNDRV_DEVICE_TYPE_PCM_PLAYBACK	SNDRV_MINOR_PCM_PLAYBACK
#define SNDRV_DEVICE_TYPE_PCM_PLOOP	SNDRV_MINOR_PCM_PLOOP
#define SNDRV_DEVICE_TYPE_PCM_CAPTURE	SNDRV_MINOR_PCM_CAPTURE
#define SNDRV_DEVICE_TYPE_PCM_CLOOP	SNDRV_MINOR_PCM_CLOOP
#define SNDRV_DEVICE_TYPE_SEQUENCER	SNDRV_MINOR_SEQUENCER
#define SNDRV_DEVICE_TYPE_TIMER		SNDRV_MINOR_TIMER

#ifdef CONFIG_SND_OSSEMUL

#define SNDRV_MINOR_OSS_DEVICES		16
#define SNDRV_MINOR_OSS_CARD(minor)	((minor) >> 4)
#define SNDRV_MINOR_OSS_DEVICE(minor)	((minor) & 0x000f)
#define SNDRV_MINOR_OSS(card, dev)	(((card) << 4) | (dev))

#define SNDRV_MINOR_OSS_MIXER		0	/* /dev/mixer - OSS 3.XX compatible */
#define SNDRV_MINOR_OSS_SEQUENCER	1	/* /dev/sequencer - OSS 3.XX compatible */
#define	SNDRV_MINOR_OSS_MIDI		2	/* /dev/midi - native midi interface - OSS 3.XX compatible - UART */
#define SNDRV_MINOR_OSS_PCM		3	/* alias */
#define SNDRV_MINOR_OSS_PCM_8		3	/* /dev/dsp - 8bit PCM - OSS 3.XX compatible */
#define SNDRV_MINOR_OSS_AUDIO		4	/* /dev/audio - SunSparc compatible */
#define SNDRV_MINOR_OSS_PCM_16		5	/* /dev/dsp16 - 16bit PCM - OSS 3.XX compatible */
#define SNDRV_MINOR_OSS_SNDSTAT		6	/* /dev/sndstat - for compatibility with OSS */
#define SNDRV_MINOR_OSS_RESERVED7	7	/* reserved for future use */
#define SNDRV_MINOR_OSS_MUSIC		8	/* /dev/music - OSS 3.XX compatible */
#define SNDRV_MINOR_OSS_DMMIDI		9	/* /dev/dmmidi0 - this device can have another minor # with OSS */
#define SNDRV_MINOR_OSS_DMFM		10	/* /dev/dmfm0 - this device can have another minor # with OSS */
#define SNDRV_MINOR_OSS_MIXER1		11	/* alternate mixer */
#define SNDRV_MINOR_OSS_PCM1		12	/* alternate PCM (GF-A-1) */
#define SNDRV_MINOR_OSS_MIDI1		13	/* alternate midi - SYNTH */
#define SNDRV_MINOR_OSS_DMMIDI1		14	/* alternate dmmidi - SYNTH */
#define SNDRV_MINOR_OSS_RESERVED15	15	/* reserved for future use */

#define SNDRV_OSS_DEVICE_TYPE_MIXER	0
#define SNDRV_OSS_DEVICE_TYPE_SEQUENCER	1
#define SNDRV_OSS_DEVICE_TYPE_PCM	2
#define SNDRV_OSS_DEVICE_TYPE_MIDI	3
#define SNDRV_OSS_DEVICE_TYPE_DMFM	4
#define SNDRV_OSS_DEVICE_TYPE_SNDSTAT	5
#define SNDRV_OSS_DEVICE_TYPE_MUSIC	6

#endif

#endif				/* __MINORS_H */
